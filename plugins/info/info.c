/* nbdkit
 * Copyright Red Hat
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
#include <string.h>
#include <sys/time.h>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#if defined (HAVE_GNUTLS) && defined (HAVE_GNUTLS_BASE64_DECODE2)
#include <gnutls/gnutls.h>
#define HAVE_BASE64 1
#endif

#define NBDKIT_API_VERSION 2

#include <nbdkit-plugin.h>

#include "ascii-string.h"
#include "byte-swapping.h"
#include "tvdiff.h"

/* The mode. */
enum mode {
  MODE_EXPORTNAME,
  MODE_BASE64EXPORTNAME,
  MODE_ADDRESS,
  MODE_TIME,
  MODE_UPTIME,
  MODE_CONNTIME,
};
static enum mode mode = MODE_EXPORTNAME;

/* Plugin load time. */
static struct timeval load_t;

static void
info_load (void)
{
  gettimeofday (&load_t, NULL);
}

static int
info_config (const char *key, const char *value)
{
  if (strcmp (key, "mode") == 0) {
    if (ascii_strcasecmp (value, "exportname") == 0 ||
        ascii_strcasecmp (value, "export-name") == 0) {
      mode = MODE_EXPORTNAME;
    }
    else if (ascii_strcasecmp (value, "base64exportname") == 0 ||
             ascii_strcasecmp (value, "base64-export-name") == 0) {
#ifdef HAVE_BASE64
      mode = MODE_BASE64EXPORTNAME;
#else
      nbdkit_error ("the plugin was compiled without base64 support");
      return -1;
#endif
    }
    else if (ascii_strcasecmp (value, "address") == 0) {
#ifdef HAVE_INET_NTOP
      mode = MODE_ADDRESS;
#else
      nbdkit_error ("the plugin was compiled without inet_ntop");
      return -1;
#endif
    }
    else if (ascii_strcasecmp (value, "time") == 0)
      mode = MODE_TIME;
    else if (ascii_strcasecmp (value, "uptime") == 0)
      mode = MODE_UPTIME;
    else if (ascii_strcasecmp (value, "conntime") == 0)
      mode = MODE_CONNTIME;
    else {
      nbdkit_error ("unknown mode: '%s'", value);
      return -1;
    }
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

#define info_config_help \
  "mode=exportname|base64exportname|address|time|uptime|conntime\n" \
  "                                      Plugin mode (default exportname)."

/* Provide a way to detect if the base64 feature is supported. */
static void
info_dump_plugin (void)
{
#ifdef HAVE_INET_NTOP
  printf ("info_address=yes\n");
#endif
#ifdef HAVE_BASE64
  printf ("info_base64=yes\n");
#endif
}

/* Per-connection handle. */
struct handle {
  void *data;                   /* Block device data. */
  size_t len;                   /* Length of data in bytes. */
  struct timeval conn_t;        /* Time since connection was opened. */
};

static int
decode_base64 (const char *data, size_t len, struct handle *ret)
{
#ifdef HAVE_BASE64
  gnutls_datum_t in, out;
  int err;

  /* For unclear reasons gnutls_base64_decode2 won't handle an empty
   * string, even though base64("") == "".
   * https://tools.ietf.org/html/rfc4648#section-10
   * https://gitlab.com/gnutls/gnutls/issues/834
   * So we have to special-case it.
   */
  if (len == 0) {
    ret->data = NULL;
    ret->len = 0;
    return 0;
  }

  in.data = (unsigned char *) data;
  in.size = len;
  err = gnutls_base64_decode2 (&in, &out);
  if (err != GNUTLS_E_SUCCESS) {
    nbdkit_error ("base64: %s", gnutls_strerror (err));
    /* We don't have to free out.data.  I verified that it is freed on
     * the error path of gnutls_base64_decode2.
     */
    return -1;
  }

  ret->data = out.data;         /* caller frees, eventually */
  ret->len = out.size;
  return 0;
#else
  nbdkit_error ("the plugin was compiled without base64 support");
  return -1;
#endif
}

static int
handle_address (struct sockaddr *sa, socklen_t addrlen,
                struct handle *ret)
{
#ifdef HAVE_INET_NTOP
  struct sockaddr_in *addr = (struct sockaddr_in *) sa;
  struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *) sa;
  union {
    char straddr[INET_ADDRSTRLEN];
    char straddr6[INET6_ADDRSTRLEN];
  } u;
  int r;
  char *str;

  switch (addr->sin_family) {
  case AF_INET:
    if (inet_ntop (AF_INET, &addr->sin_addr,
                   u.straddr, sizeof u.straddr) == NULL) {
      nbdkit_error ("inet_ntop: %m");
      return -1;
    }
    r = asprintf (&str, "%s:%d", u.straddr, ntohs (addr->sin_port));
    if (r == -1) {
      nbdkit_error ("asprintf: %m");
      return -1;
    }
    ret->len = r;
    ret->data = str;
    return 0;

  case AF_INET6:
    if (inet_ntop (AF_INET6, &addr6->sin6_addr,
                   u.straddr6, sizeof u.straddr6) == NULL) {
      nbdkit_error ("inet_ntop: %m");
      return -1;
    }
    r = asprintf (&str, "[%s]:%d", u.straddr6, ntohs (addr6->sin6_port));
    if (r == -1) {
      nbdkit_error ("asprintf: %m");
      return -1;
    }
    ret->len = r;
    ret->data = str;
    return 0;

  case AF_UNIX:
    /* We don't want to expose the socket path because it's a host
     * filesystem name.  The client might not really be running on the
     * same machine (eg. it is using a proxy).  However it doesn't
     * even matter because getpeername(2) on Linux returns a zero
     * length sun_path in this case!
     */
    str = strdup ("unix");
    if (str == NULL) {
      nbdkit_error ("strdup: %m");
      return -1;
    }
    ret->len = strlen (str);
    ret->data = str;
    return 0;

  default:
    nbdkit_debug ("unsupported socket family %d", addr->sin_family);
    ret->data = NULL;
    ret->len = 0;
    return 0;
  }
#else
  nbdkit_error ("the plugin was compiled without inet_ntop");
  return -1;
#endif
}

/* Create the per-connection handle.
 *
 * This is a rather unusual plugin because it has to parse data sent
 * by the client.  For security reasons, be careful about:
 *
 * - Returning more data than is sent by the client.
 *
 * - Inputs that result in unbounded output.
 *
 * - Inputs that could hang, crash or exploit the server.
 *
 * - Leaking host information (eg. paths).
 */
static void *
info_open (int readonly)
{
  const char *export_name;
  size_t export_name_len;
  struct sockaddr_storage addr;
  socklen_t addrlen;
  struct handle *h;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  switch (mode) {
  case MODE_EXPORTNAME:
  case MODE_BASE64EXPORTNAME:
    export_name = nbdkit_export_name ();
    if (export_name == NULL) {
      free (h);
      return NULL;
    }
    export_name_len = strlen (export_name);

    if (mode == MODE_EXPORTNAME) {
      h->len = export_name_len;
      h->data = strdup (export_name);
      if (h->data == NULL) {
        nbdkit_error ("strdup: %m");
        free (h);
        return NULL;
      }
      return h;
    }
    else /* mode == MODE_BASE64EXPORTNAME */ {
      if (decode_base64 (export_name, export_name_len, h) == -1) {
        free (h);
        return NULL;
      }
      return h;
    }

  case MODE_ADDRESS:
    addrlen = sizeof addr;
    if (nbdkit_peer_name ((struct sockaddr *) &addr, &addrlen) == -1 ||
        handle_address ((struct sockaddr *) &addr, addrlen, h) == -1) {
      free (h);
      return NULL;
    }
    return h;

  case MODE_TIME:
  case MODE_UPTIME:
  case MODE_CONNTIME:
    gettimeofday (&h->conn_t, NULL);
    h->len = 12;
    h->data = malloc (h->len);
    if (h->data == NULL) {
      nbdkit_error ("malloc: %m");
      free (h);
      return NULL;
    }
    return h;

  default:
    abort ();
  }
}

/* Close the per-connection handle. */
static void
info_close (void *handle)
{
  struct handle *h = handle;

  free (h->data);
  free (h);
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Get the disk size. */
static int64_t
info_get_size (void *handle)
{
  struct handle *h = handle;

  return (int64_t) h->len;
}

static int
info_can_multi_conn (void *handle)
{
  switch (mode) {
    /* Safe for exportname modes since clients should only request
     * multi-conn with the same export name.
     */
  case MODE_EXPORTNAME:
  case MODE_BASE64EXPORTNAME:
    return 1;
    /* Unsafe for mode=address because all multi-conn connections
     * won't necessarily originate from the same client address.
     */
  case MODE_ADDRESS:
    return 0;
    /* All time modes will read different values at different times,
     * so all of them are unsafe for multi-conn.
     */
  case MODE_TIME:
  case MODE_UPTIME:
  case MODE_CONNTIME:
    return 0;

    /* Keep GCC happy. */
  default:
    abort ();
  }
}

/* Cache. */
static int
info_can_cache (void *handle)
{
  /* Everything is already in memory, returning this without
   * implementing .cache lets nbdkit do the correct no-op.
   */
  return NBDKIT_CACHE_NATIVE;
}

static void
update_time (struct handle *h)
{
  struct timeval tv;
  int64_t secs;
  int32_t usecs;
  char *p;

  gettimeofday (&tv, NULL);

  switch (mode) {
  case MODE_TIME:
    break;

  case MODE_UPTIME:
    subtract_timeval (&load_t, &tv, &tv);
    break;

  case MODE_CONNTIME:
    subtract_timeval (&h->conn_t, &tv, &tv);
    break;

  default:
    abort ();
  }

  /* Pack the result into the output buffer. */
  secs = tv.tv_sec;
  usecs = tv.tv_usec;
  secs = htobe64 (secs);
  usecs = htobe32 (usecs);
  p = h->data;
  memcpy (&p[0], &secs, 8);
  memcpy (&p[8], &usecs, 4);
}

/* Read data. */
static int
info_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
            uint32_t flags)
{
  struct handle *h = handle;

  /* For the time modes we update the data on every read. */
  if (mode == MODE_TIME || mode == MODE_UPTIME || mode == MODE_CONNTIME)
    update_time (h);

  memcpy (buf, h->data + offset, count);
  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "info",
  .version           = PACKAGE_VERSION,
  .load              = info_load,
  .config            = info_config,
  .config_help       = info_config_help,
  .dump_plugin       = info_dump_plugin,
  .magic_config_key  = "mode",
  .open              = info_open,
  .close             = info_close,
  .get_size          = info_get_size,
  .can_multi_conn    = info_can_multi_conn,
  .can_cache         = info_can_cache,
  .pread             = info_pread,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN (plugin)
