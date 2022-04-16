/* nbdkit
 * Copyright (C) 2019 Red Hat Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of Red Hat nor the names of its contributors may be
 * used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_LINUX_VM_SOCKETS_H
#include <linux/vm_sockets.h>
#elif HAVE_SYS_VSOCK_H
#include <sys/vsock.h>
#endif

#include <nbdkit-filter.h>

#include "ascii-string.h"
#include "cleanup.h"

/* -D ip.rules=1 to enable debugging of rules and rule matching. */
NBDKIT_DLL_PUBLIC int ip_debug_rules;

struct rule {
  struct rule *next;
  enum { BAD = 0,
         ANY, ANYV4, ANYV6, IPV4, IPV6,
         ANYUNIX, PID, UID, GID,
         ANYVSOCK, VSOCKCID, VSOCKPORT } type;
  union {
    struct in_addr ipv4;        /* for IPV4, IPV6 */
    struct in6_addr ipv6;
    int64_t id;                 /* for PID, UID, GID, VSOCKCID, VSOCKPORT */
  } u;
  unsigned prefixlen;           /* for IPV4, IPV6 */
};

static struct rule *allow_rules, *allow_rules_last;
static struct rule *deny_rules, *deny_rules_last;

static void
print_rule (const char *name, const struct rule *rule, const char *suffix)
{
  union {
    char addr4[INET_ADDRSTRLEN];
    char addr6[INET6_ADDRSTRLEN];
  } u;

  switch (rule->type) {
  case ANY:
    nbdkit_debug ("%s=any%s", name, suffix);
    break;
  case ANYV4:
    nbdkit_debug ("%s=anyipv4%s", name, suffix);
    break;
  case ANYV6:
    nbdkit_debug ("%s=anyipv6%s", name, suffix);
    break;
  case IPV4:
    inet_ntop (AF_INET, &rule->u.ipv4, u.addr4, sizeof u.addr4);
    nbdkit_debug ("%s=ipv4:%s/%u%s", name, u.addr4, rule->prefixlen, suffix);
    break;
  case IPV6:
    inet_ntop (AF_INET6, &rule->u.ipv6, u.addr6, sizeof u.addr6);
    nbdkit_debug ("%s=ipv6:[%s]/%u%s", name, u.addr6, rule->prefixlen, suffix);
    break;

  case ANYUNIX:
    nbdkit_debug ("%s=anyunix%s", name, suffix);
    break;
  case PID:
    nbdkit_debug ("%s=pid:%" PRIi64 "%s", name, rule->u.id, suffix);
    break;
  case UID:
    nbdkit_debug ("%s=uid:%" PRIi64 "%s", name, rule->u.id, suffix);
    break;
  case GID:
    nbdkit_debug ("%s=gid:%" PRIi64 "%s", name, rule->u.id, suffix);
    break;

  case ANYVSOCK:
    nbdkit_debug ("%s=anyvsock%s", name, suffix);
    break;
  case VSOCKCID:
    nbdkit_debug ("%s=vsock-cid:%" PRIi64 "%s", name, rule->u.id, suffix);
    break;
  case VSOCKPORT:
    nbdkit_debug ("%s=vsock-port:%" PRIi64 "%s", name, rule->u.id, suffix);
    break;

  case BAD:
    nbdkit_debug ("%s=BAD(!)%s", name, suffix);
    break;
  default:
    nbdkit_debug ("%s=UNKNOWN RULE TYPE(!)%s", name, suffix);
  }
}

static void
print_rules (const char *name, const struct rule *rules)
{
  const struct rule *rule;

  for (rule = rules; rule != NULL; rule = rule->next)
    print_rule (name, rule, "");
}

static void
free_rules (struct rule *rules)
{
  struct rule *rule, *next;

  for (rule = rules; rule != NULL; rule = next) {
    next = rule->next;
    free (rule);
  }
}

static void
ip_unload (void)
{
  free_rules (allow_rules);
  free_rules (deny_rules);
}

/* Try to parse the first n characters of value as an IPv4 or IPv6
 * address.  Returns: IPV4 or IPV6 if the parse was successful, with
 * the address being written into the rule struct.  Returns 0 if we
 * were unable to parse it.
 */
static int
parse_ip_address (const char *value, size_t n, struct rule *ret)
{
#define MAX_ADDRLEN 64
  char addr[MAX_ADDRLEN+1];

  if (n > MAX_ADDRLEN)
    return 0;                /* Value too long to be an IP address. */

  /* We have to copy it to our own buffer because inet_pton cannot
   * parse a buffer which is not \0-terminated.
   */
  strncpy (addr, value, n);
  addr[n] = '\0';

  if (inet_pton (AF_INET, addr, &ret->u.ipv4) == 1)
    return IPV4;

  if (inet_pton (AF_INET6, addr, &ret->u.ipv6) == 1)
    return IPV6;

  return 0;
}

/* Try to parse an unsigned from the first n characters of value.
 * Returns 0 if successful or -1 on error.  Basically a wrapper around
 * nbdkit_parse_unsigned.
 */
static int
parse_unsigned (const char *paramname,
                const char *value, size_t n, unsigned *ret)
{
#define MAX_LEN 32
  char buf[MAX_LEN+1];

  if (n > MAX_LEN) {
    nbdkit_error ("%s: cannot parse prefix length: %.*s",
                  paramname, (int) n, value);
    return -1;
  }

  strncpy (buf, value, n);
  buf[n] = '\0';

  return nbdkit_parse_unsigned (paramname, buf, ret);
}

static int
parse_rule (const char *paramname,
            struct rule **rules, struct rule **rules_last,
            const char *value, size_t n)
{
  struct rule *new_rule;
  const char *p;
  int type;

  new_rule = calloc (1, sizeof *new_rule);
  if (new_rule == NULL) {
    nbdkit_error ("calloc: %m");
    return -1;
  }
  if (*rules == NULL)
    *rules = new_rule;
  else
    (*rules_last)->next = new_rule;
  *rules_last = new_rule;

  assert (n > 0);

  if (n == 3 && (ascii_strncasecmp (value, "all", 3) == 0 ||
                 ascii_strncasecmp (value, "any", 3) == 0)) {
    new_rule->type = ANY;
    return 0;
  }

  if (n == 7 && (ascii_strncasecmp (value, "allipv4", 7) == 0 ||
                 ascii_strncasecmp (value, "anyipv4", 7) == 0)) {
    new_rule->type = ANYV4;
    return 0;
  }

  if (n == 7 && (ascii_strncasecmp (value, "allipv6", 7) == 0 ||
                 ascii_strncasecmp (value, "anyipv6", 7) == 0)) {
    new_rule->type = ANYV6;
    return 0;
  }

  if (n == 7 && (ascii_strncasecmp (value, "allunix", 7) == 0 ||
                 ascii_strncasecmp (value, "anyunix", 7) == 0)) {
    new_rule->type = ANYUNIX;
    return 0;
  }

  if (n >= 4 && ascii_strncasecmp (value, "pid:", 4) == 0) {
    new_rule->type = PID;
    if (nbdkit_parse_int64_t ("pid:", &value[4], &new_rule->u.id) == -1)
      return -1;
    if (new_rule->u.id <= 0) {
      nbdkit_error ("pid: parameter out of range");
      return -1;
    }
    return 0;
  }
  if (n >= 4 && ascii_strncasecmp (value, "uid:", 4) == 0) {
    new_rule->type = UID;
    if (nbdkit_parse_int64_t ("uid:", &value[4], &new_rule->u.id) == -1)
      return -1;
    if (new_rule->u.id < 0) {
      nbdkit_error ("uid: parameter out of range");
      return -1;
    }
    return 0;
  }
  if (n >= 4 && ascii_strncasecmp (value, "gid:", 4) == 0) {
    new_rule->type = GID;
    if (nbdkit_parse_int64_t ("gid:", &value[4], &new_rule->u.id) == -1)
      return -1;
    if (new_rule->u.id < 0) {
      nbdkit_error ("gid: parameter out of range");
      return -1;
    }
    return 0;
  }

  if (n == 8 && (ascii_strncasecmp (value, "allvsock", 8) == 0 ||
                 ascii_strncasecmp (value, "anyvsock", 8) == 0)) {
    new_rule->type = ANYVSOCK;
    return 0;
  }
  if (n >= 10 && ascii_strncasecmp (value, "vsock-cid:", 10) == 0) {
    new_rule->type = VSOCKCID;
    if (nbdkit_parse_int64_t ("vsock-cid:", &value[10], &new_rule->u.id) == -1)
      return -1;
    if (new_rule->u.id < 0 || new_rule->u.id > UINT32_MAX) {
      nbdkit_error ("vsock-cid: parameter out of range");
      return -1;
    }
    return 0;
  }
  if (n >= 11 && ascii_strncasecmp (value, "vsock-port:", 11) == 0) {
    new_rule->type = VSOCKPORT;
    if (nbdkit_parse_int64_t ("vsock-port:", &value[11], &new_rule->u.id) == -1)
      return -1;
    if (new_rule->u.id < 0 || new_rule->u.id > UINT32_MAX) {
      nbdkit_error ("vsock-port: parameter out of range");
      return -1;
    }
    return 0;
  }

  /* Address with prefixlen. */
  if ((p = strchr (value, '/')) != NULL) {
    size_t pllen = &value[n] - &p[1];
    size_t addrlen = p - value;

    /* Try parsing the prefixlen. */
    if (parse_unsigned (paramname, p+1, pllen, &new_rule->prefixlen) == -1)
      return -1;

    /* Try parsing the address as IPv4 or IPv6. */
    type = parse_ip_address (value, addrlen, new_rule);
    if (type == IPV4) {
      if (new_rule->prefixlen > 32) {
        nbdkit_error ("prefix is > 32 in %s=%.*s",
                      paramname, (int) n, value);
        return -1;
      }
      new_rule->type = IPV4;
    }
    else if (type == IPV6) {
      if (new_rule->prefixlen > 128) {
        nbdkit_error ("prefix is > 128 in %s=%.*s",
                      paramname, (int) n, value);
        return -1;
      }
      new_rule->type = IPV6;
    }
    else {
      nbdkit_error ("cannot parse address \"%.*s\" from %s=%.*s",
                    (int) addrlen, value, paramname, (int) n, value);
      return -1;
    }

    return 0;
  }

  /* IPv4 or IPv6 address without prefixlen. */
  type = parse_ip_address (value, n, new_rule);
  if (type == IPV4) {
    new_rule->prefixlen = 32;
    new_rule->type = IPV4;
    return 0;
  }
  else if (type == IPV6) {
    new_rule->prefixlen = 128;
    new_rule->type = IPV6;
    return 0;
  }

  nbdkit_error ("don't know how to parse rule: %s=%.*s",
                paramname, (int) n, value);
  return -1;
}

static int
parse_rules (const char *paramname,
             struct rule **rules, struct rule **rules_last,
             const char *value)
{
  size_t n;

  while (*value != '\0') {
    n = strcspn (value, ",");
    if (n == 0) {
      nbdkit_error ("%s: empty entry in rule list", paramname);
      return -1;
    }
    if (parse_rule (paramname, rules, rules_last, value, n) == -1)
      return -1;
    value += n;
    if (*value == ',')
      value++;
  }

  return 0;
}

static int
ip_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
           const char *key, const char *value)
{
  /* For convenience we permit multiple allow and deny parameters,
   * which append rules to the end of the respective list.
   */
  if (strcmp (key, "allow") == 0) {
    if (parse_rules (key, &allow_rules, &allow_rules_last, value) == -1)
      return -1;
    return 0;
  }
  else if (strcmp (key, "deny") == 0) {
    if (parse_rules (key, &deny_rules, &deny_rules_last, value) == -1)
      return -1;
    return 0;
  }

  return next (nxdata, key, value);
}

static int
ip_config_complete (nbdkit_next_config_complete *next, nbdkit_backend *nxdata)
{
  if (ip_debug_rules) {
    print_rules ("ip: parsed allow", allow_rules);
    print_rules ("ip: parsed deny", deny_rules);
  }

  return next (nxdata);
}

#define ip_config_help \
  "allow=addr[,addr...]     Set allow list.\n" \
  "deny=addr[,addr...]      Set deny list."

/* Compare two IPv6 addresses as far as prefixlen bits.  Both
 * addresses are in network byte order (ie. big endian) so we can
 * compare them byte by byte from the beginning without considering
 * host endianness.
 */
static bool
ipv6_equal (struct in6_addr addr1, struct in6_addr addr2, unsigned prefixlen)
{
  size_t n;

  for (n = 0; n < 16; ++n) {
    if (prefixlen == 0)
      return true;

    if (prefixlen < 8) {
      const uint8_t mask = 0xff << (8 - prefixlen);
      return (addr1.s6_addr[n] & mask) == (addr2.s6_addr[n] & mask);
    }

    if (addr1.s6_addr[n] != addr2.s6_addr[n])
      return false;

    prefixlen -= 8;
  }

  assert (prefixlen == 0);

  return true;
}

static bool
matches_rule (const struct rule *rule,
              int family, const struct sockaddr *addr)
{
  const struct sockaddr_in *sin;
  uint32_t cin, rin, mask;
  const struct sockaddr_in6 *sin6;
#if defined(AF_VSOCK) && defined(VMADDR_CID_ANY)
  const struct sockaddr_vm *svm;
#endif

  switch (rule->type) {
  case ANY:
    return true;

  case ANYV4:
    return family == AF_INET;

  case ANYV6:
    return family == AF_INET6;

  case IPV4:
    if (family != AF_INET) return false;
    sin = (struct sockaddr_in *) addr;
    cin = ntohl (sin->sin_addr.s_addr);
    rin = ntohl (rule->u.ipv4.s_addr);
    mask = 0xffffffff << (32 - rule->prefixlen);
    return (cin & mask) == (rin & mask);

  case IPV6:
    if (family != AF_INET6) return false;
    sin6 = (struct sockaddr_in6 *) addr;
    return ipv6_equal (sin6->sin6_addr, rule->u.ipv6, rule->prefixlen);

  case ANYUNIX:
    return family == AF_UNIX;

    /* Note these work even if the underlying nbdkit_peer_* call fails. */
  case PID:
    if (family != AF_UNIX) return false;
    return nbdkit_peer_pid () == rule->u.id;

  case UID:
    if (family != AF_UNIX) return false;
    return nbdkit_peer_uid () == rule->u.id;

  case GID:
    if (family != AF_UNIX) return false;
    return nbdkit_peer_gid () == rule->u.id;

#if defined(AF_VSOCK) && defined(VMADDR_CID_ANY)
  case ANYVSOCK:
    return family == AF_VSOCK;

  case VSOCKCID:
    if (family != AF_VSOCK) return false;
    svm = (struct sockaddr_vm *) addr;
    return svm->svm_cid == rule->u.id;

  case VSOCKPORT:
    if (family != AF_VSOCK) return false;
    svm = (struct sockaddr_vm *) addr;
    return svm->svm_port == rule->u.id;

#else /* !AF_VSOCK */
  case ANYVSOCK:
  case VSOCKCID:
  case VSOCKPORT:
    return false;
#endif

  case BAD:
  default:
    abort ();
  }
}

static bool
matches_rules_list (const char *name, const struct rule *rules,
                    int family, const struct sockaddr *addr)
{
  const struct rule *rule;
  bool b;

  for (rule = rules; rule != NULL; rule = rule->next) {
    b = matches_rule (rule, family, addr);
    if (ip_debug_rules)
      print_rule (name, rule, b ? " => yes" : " => no");
    if (b)
      return true;
  }

  return false;
}

static bool
check_if_allowed (const struct sockaddr *addr)
{
  int family = ((struct sockaddr_in *)addr)->sin_family;

  /* There's an implicit allow all for non-IP, non-Unix, non-AF_VSOCK
   * sockets, see the manual.
   */
  if (family != AF_INET && family != AF_INET6 && family != AF_UNIX
#if defined(AF_VSOCK) && defined(VMADDR_CID_ANY)
      && family != AF_VSOCK
#endif
      )
    return true;

  if (matches_rules_list ("ip: match source with allow",
                          allow_rules, family, addr))
    return true;

  if (matches_rules_list ("ip: match source with deny",
                          deny_rules, family, addr))
    return false;

  return true;
}

static int
ip_preconnect (nbdkit_next_preconnect *next, nbdkit_backend *nxdata,
               int readonly)
{
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof addr;

  if (nbdkit_peer_name ((struct sockaddr *) &addr, &addrlen) == -1)
    return -1;                  /* We should fail closed ... */

  /* Follow the rules. */
  if (check_if_allowed ((struct sockaddr *) &addr) == false) {
    nbdkit_error ("client not permitted to connect "
                  "because of source address restriction");
    return -1;
  }

  if (next (nxdata, readonly) == -1)
    return -1;

  return 0;
}

static int
ip_thread_model (void)
{
  return NBDKIT_THREAD_MODEL_PARALLEL;
}

static struct nbdkit_filter filter = {
  .name              = "ip",
  .longname          = "nbdkit ip filter",
  .unload            = ip_unload,
  .thread_model      = ip_thread_model,
  .config            = ip_config,
  .config_complete   = ip_config_complete,
  .config_help       = ip_config_help,
  .preconnect        = ip_preconnect,
};

NBDKIT_REGISTER_FILTER(filter)
