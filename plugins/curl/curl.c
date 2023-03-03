/* nbdkit
 * Copyright (C) 2014-2023 Red Hat Inc.
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
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <curl/curl.h>

#include <nbdkit-plugin.h>

#include "cleanup.h"

#include "curldefs.h"

/* Plugin configuration. */
const char *url = NULL;         /* required */

const char *cainfo = NULL;
const char *capath = NULL;
unsigned connections = 4;
char *cookie = NULL;
const char *cookiefile = NULL;
const char *cookiejar = NULL;
const char *cookie_script = NULL;
unsigned cookie_script_renew = 0;
bool followlocation = true;
struct curl_slist *headers = NULL;
const char *header_script = NULL;
unsigned header_script_renew = 0;
long http_version = CURL_HTTP_VERSION_NONE;
char *password = NULL;
#ifndef HAVE_CURLOPT_PROTOCOLS_STR
long protocols = CURLPROTO_ALL;
#else
const char *protocols = NULL;
#endif
const char *proxy = NULL;
char *proxy_password = NULL;
const char *proxy_user = NULL;
bool sslverify = true;
const char *ssl_cipher_list = NULL;
long ssl_version = CURL_SSLVERSION_DEFAULT;
const char *tls13_ciphers = NULL;
bool tcp_keepalive = false;
bool tcp_nodelay = true;
uint32_t timeout = 0;
const char *unix_socket_path = NULL;
const char *user = NULL;
const char *user_agent = NULL;

/* Use '-D curl.verbose=1' to set. */
NBDKIT_DLL_PUBLIC int curl_debug_verbose = 0;

static void
curl_load (void)
{
  CURLcode r;

  r = curl_global_init (CURL_GLOBAL_DEFAULT);
  if (r != CURLE_OK) {
    nbdkit_error ("libcurl initialization failed: %d", (int) r);
    exit (EXIT_FAILURE);
  }
}

static void
curl_unload (void)
{
  free (cookie);
  if (headers)
    curl_slist_free_all (headers);
  free (password);
  free (proxy_password);
  scripts_unload ();
  free_all_handles ();
  curl_global_cleanup ();
}

#ifndef HAVE_CURLOPT_PROTOCOLS_STR
/* See <curl/curl.h> */
static struct { const char *name; long bitmask; } curl_protocols[] = {
  { "http", CURLPROTO_HTTP },
  { "https", CURLPROTO_HTTPS },
  { "ftp", CURLPROTO_FTP },
  { "ftps", CURLPROTO_FTPS },
  { "scp", CURLPROTO_SCP },
  { "sftp", CURLPROTO_SFTP },
  { "telnet", CURLPROTO_TELNET },
  { "ldap", CURLPROTO_LDAP },
  { "ldaps", CURLPROTO_LDAPS },
  { "dict", CURLPROTO_DICT },
  { "file", CURLPROTO_FILE },
  { "tftp", CURLPROTO_TFTP },
  { "imap", CURLPROTO_IMAP },
  { "imaps", CURLPROTO_IMAPS },
  { "pop3", CURLPROTO_POP3 },
  { "pop3s", CURLPROTO_POP3S },
  { "smtp", CURLPROTO_SMTP },
  { "smtps", CURLPROTO_SMTPS },
  { "rtsp", CURLPROTO_RTSP },
  { "rtmp", CURLPROTO_RTMP },
  { "rtmpt", CURLPROTO_RTMPT },
  { "rtmpe", CURLPROTO_RTMPE },
  { "rtmpte", CURLPROTO_RTMPTE },
  { "rtmps", CURLPROTO_RTMPS },
  { "rtmpts", CURLPROTO_RTMPTS },
  { "gopher", CURLPROTO_GOPHER },
#ifdef CURLPROTO_SMB
  { "smb", CURLPROTO_SMB },
#endif
#ifdef CURLPROTO_SMBS
  { "smbs", CURLPROTO_SMBS },
#endif
#ifdef CURLPROTO_MQTT
  { "mqtt", CURLPROTO_MQTT },
#endif
  { NULL }
};

/* Parse the protocols parameter. */
static int
parse_protocols (const char *value)
{
  size_t n, i;

  protocols = 0;

  while (*value) {
    n = strcspn (value, ",");
    for (i = 0; curl_protocols[i].name != NULL; ++i) {
      if (strlen (curl_protocols[i].name) == n &&
          strncmp (value, curl_protocols[i].name, n) == 0) {
        protocols |= curl_protocols[i].bitmask;
        goto found;
      }
    }
    nbdkit_error ("protocols: protocol name not found: %.*s", (int) n, value);
    return -1;

  found:
    value += n;
    if (*value == ',')
      value++;
  }

  if (protocols == 0) {
    nbdkit_error ("protocols: empty list of protocols is not allowed");
    return -1;
  }

  nbdkit_debug ("curl: protocols: %ld", protocols);

  return 0;
}
#endif /* !HAVE_CURLOPT_PROTOCOLS_STR */

/* Called for each key=value passed on the command line. */
static int
curl_config (const char *key, const char *value)
{
  int r;

  if (strcmp (key, "cainfo") == 0) {
    cainfo = value;
  }

  else if (strcmp (key, "capath") == 0) {
    capath =  value;
  }

  else if (strcmp (key, "connections") == 0) {
    if (nbdkit_parse_unsigned ("connections", value,
                               &connections) == -1)
      return -1;
    if (connections == 0) {
      nbdkit_error ("connections parameter must not be 0");
      return -1;
    }
  }

  else if (strcmp (key, "cookie") == 0) {
    free (cookie);
    if (nbdkit_read_password (value, &cookie) == -1)
      return -1;
  }

  else if (strcmp (key, "cookiefile") == 0) {
    /* Reject cookiefile=- because it will cause libcurl to try to
     * read from stdin when we connect.
     */
    if (strcmp (value, "-") == 0) {
      nbdkit_error ("cookiefile parameter cannot be \"-\"");
      return -1;
    }
    cookiefile = value;
  }

  else if (strcmp (key, "cookiejar") == 0) {
    /* Reject cookiejar=- because it will cause libcurl to try to
     * write to stdout.
     */
    if (strcmp (value, "-") == 0) {
      nbdkit_error ("cookiejar parameter cannot be \"-\"");
      return -1;
    }
    cookiejar = value;
  }

  else if (strcmp (key, "cookie-script") == 0) {
    cookie_script = value;
  }

  else if (strcmp (key, "cookie-script-renew") == 0) {
    if (nbdkit_parse_unsigned ("cookie-script-renew", value,
                               &cookie_script_renew) == -1)
      return -1;
  }

  else if (strcmp (key, "followlocation") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    followlocation = r;
  }

  else if (strcmp (key, "header") == 0) {
    headers = curl_slist_append (headers, value);
    if (headers == NULL) {
      nbdkit_error ("curl_slist_append: %m");
      return -1;
    }
  }

  else if (strcmp (key, "header-script") == 0) {
    header_script = value;
  }

  else if (strcmp (key, "header-script-renew") == 0) {
    if (nbdkit_parse_unsigned ("header-script-renew", value,
                               &header_script_renew) == -1)
      return -1;
  }

  else if (strcmp (key, "http-version") == 0) {
    if (strcmp (value, "none") == 0)
      http_version = CURL_HTTP_VERSION_NONE;
    else if (strcmp (value, "1.0") == 0)
      http_version = CURL_HTTP_VERSION_1_0;
    else if (strcmp (value, "1.1") == 0)
      http_version = CURL_HTTP_VERSION_1_1;
#ifdef HAVE_CURL_HTTP_VERSION_2_0
    else if (strcmp (value, "2.0") == 0)
      http_version = CURL_HTTP_VERSION_2_0;
#endif
#ifdef HAVE_CURL_HTTP_VERSION_2TLS
    else if (strcmp (value, "2TLS") == 0)
      http_version = CURL_HTTP_VERSION_2TLS;
#endif
#ifdef HAVE_CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE
    else if (strcmp (value, "2-prior-knowledge") == 0)
      http_version = CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE;
#endif
#ifdef HAVE_CURL_HTTP_VERSION_3
    else if (strcmp (value, "3") == 0)
      http_version = CURL_HTTP_VERSION_3;
#endif
#ifdef HAVE_CURL_HTTP_VERSION_3ONLY
    else if (strcmp (value, "3only") == 0)
      http_version = CURL_HTTP_VERSION_3ONLY;
#endif
    else {
      nbdkit_error ("unknown http-version: %s", value);
      return -1;
    }
  }

  else if (strcmp (key, "password") == 0) {
    free (password);
    if (nbdkit_read_password (value, &password) == -1)
      return -1;
  }

  else if (strcmp (key, "protocols") == 0) {
#ifndef HAVE_CURLOPT_PROTOCOLS_STR
    if (parse_protocols (value) == -1)
      return -1;
#else
    protocols = value;
#endif
  }

  else if (strcmp (key, "proxy") == 0) {
    proxy = value;
  }

  else if (strcmp (key, "proxy-password") == 0) {
    free (proxy_password);
    if (nbdkit_read_password (value, &proxy_password) == -1)
      return -1;
  }

  else if (strcmp (key, "proxy-user") == 0)
    proxy_user = value;

  else if (strcmp (key, "sslverify") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    sslverify = r;
  }

  else if (strcmp (key, "ssl-version") == 0) {
    if (strcmp (value, "default") == 0)
      ssl_version = CURL_SSLVERSION_DEFAULT;
    else if (strcmp (value, "tlsv1") == 0)
      ssl_version = CURL_SSLVERSION_TLSv1;
    else if (strcmp (value, "sslv2") == 0)
      ssl_version = CURL_SSLVERSION_SSLv2;
    else if (strcmp (value, "sslv3") == 0)
      ssl_version = CURL_SSLVERSION_SSLv3;
    else if (strcmp (value, "tlsv1.0") == 0)
      ssl_version = CURL_SSLVERSION_TLSv1_0;
    else if (strcmp (value, "tlsv1.1") == 0)
      ssl_version = CURL_SSLVERSION_TLSv1_1;
    else if (strcmp (value, "tlsv1.2") == 0)
      ssl_version = CURL_SSLVERSION_TLSv1_2;
    else if (strcmp (value, "tlsv1.3") == 0)
      ssl_version = CURL_SSLVERSION_TLSv1_3;
#ifdef HAVE_CURL_SSLVERSION_MAX_DEFAULT
    else if (strcmp (value, "max-default") == 0)
      ssl_version = CURL_SSLVERSION_MAX_DEFAULT;
#endif
#ifdef HAVE_CURL_SSLVERSION_MAX_TLSv1_0
    else if (strcmp (value, "max-tlsv1.0") == 0)
      ssl_version = CURL_SSLVERSION_MAX_TLSv1_0;
#endif
#ifdef HAVE_CURL_SSLVERSION_MAX_TLSv1_1
    else if (strcmp (value, "max-tlsv1.1") == 0)
      ssl_version = CURL_SSLVERSION_MAX_TLSv1_1;
#endif
#ifdef HAVE_CURL_SSLVERSION_MAX_TLSv1_2
    else if (strcmp (value, "max-tlsv1.2") == 0)
      ssl_version = CURL_SSLVERSION_MAX_TLSv1_2;
#endif
#ifdef HAVE_CURL_SSLVERSION_MAX_TLSv1_3
    else if (strcmp (value, "max-tlsv1.3") == 0)
      ssl_version = CURL_SSLVERSION_MAX_TLSv1_3;
#endif
    else {
      nbdkit_error ("unknown ssl-version: %s", value);
      return -1;
    }
  }

  else if (strcmp (key, "ssl-cipher-list") == 0)
    ssl_cipher_list = value;

  else if (strcmp (key, "tls13-ciphers") == 0)
    tls13_ciphers = value;

  else if (strcmp (key, "tcp-keepalive") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    tcp_keepalive = r;
  }

  else if (strcmp (key, "tcp-nodelay") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    tcp_nodelay = r;
  }

  else if (strcmp (key, "timeout") == 0) {
    if (nbdkit_parse_uint32_t ("timeout", value, &timeout) == -1)
      return -1;
#if LONG_MAX < UINT32_MAX
    /* C17 5.2.4.2.1 requires that LONG_MAX is at least 2^31 - 1.
     * However a large positive number might still exceed the limit.
     */
    if (timeout > LONG_MAX) {
      nbdkit_error ("timeout is too large");
      return -1;
    }
#endif
  }

  else if (strcmp (key, "unix-socket-path") == 0 ||
           strcmp (key, "unix_socket_path") == 0)
    unix_socket_path = value;

  else if (strcmp (key, "url") == 0)
    url = value;

  else if (strcmp (key, "user") == 0)
    user = value;

  else if (strcmp (key, "user-agent") == 0)
    user_agent = value;

  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

/* Check the user did pass a url parameter. */
static int
curl_config_complete (void)
{
  if (url == NULL) {
    nbdkit_error ("you must supply the url=<URL> parameter "
                  "after the plugin name on the command line");
    return -1;
  }

  if (headers && header_script) {
    nbdkit_error ("header and header-script cannot be used at the same time");
    return -1;
  }

  if (!header_script && header_script_renew) {
    nbdkit_error ("header-script-renew cannot be used without header-script");
    return -1;
  }

  if (cookie && cookie_script) {
    nbdkit_error ("cookie and cookie-script cannot be used at the same time");
    return -1;
  }

  if (!cookie_script && cookie_script_renew) {
    nbdkit_error ("cookie-script-renew cannot be used without cookie-script");
    return -1;
  }

  return 0;
}

#define curl_config_help \
  "cainfo=<CAINFO>            Path to Certificate Authority file.\n" \
  "capath=<CAPATH>            Path to directory with CA certificates.\n" \
  "connections=<N>            Number of libcurl connections to use.\n" \
  "cookie=<COOKIE>            Set HTTP/HTTPS cookies.\n" \
  "cookiefile=                Enable cookie processing.\n" \
  "cookiefile=<FILENAME>      Read cookies from file.\n" \
  "cookiejar=<FILENAME>       Read and write cookies to jar.\n" \
  "cookie-script=<SCRIPT>     Script to set HTTP/HTTPS cookies.\n" \
  "cookie-script-renew=<SECS> Time to renew HTTP/HTTPS cookies.\n" \
  "followlocation=false       Do not follow redirects.\n" \
  "header=<HEADER>            Set HTTP/HTTPS header.\n" \
  "header-script=<SCRIPT>     Script to set HTTP/HTTPS headers.\n" \
  "header-script-renew=<SECS> Time to renew HTTP/HTTPS headers.\n" \
  "http-version=none|...      Force a particular HTTP protocol.\n" \
  "password=<PASSWORD>        The password for the user account.\n" \
  "protocols=PROTO,PROTO,..   Limit protocols allowed.\n" \
  "proxy=<PROXY>              Set proxy URL.\n" \
  "proxy-password=<PASSWORD>  The proxy password.\n" \
  "proxy-user=<USER>          The proxy user.\n" \
  "sslverify=false            Do not verify SSL certificate of remote host.\n" \
  "ssl-cipher-list=C1:C2:..   Specify TLS/SSL cipher suites to be used.\n" \
  "ssl-version=<VERSION>      Specify preferred TLS/SSL version.\n" \
  "tcp-keepalive=true         Enable TCP keepalives.\n" \
  "tcp-nodelay=false          Disable Nagle’s algorithm.\n" \
  "timeout=<TIMEOUT>          Set the timeout for requests (seconds).\n" \
  "tls13-ciphers=C1:C2:..     Specify TLS 1.3 cipher suites to be used.\n" \
  "unix-socket-path=<PATH>    Open Unix domain socket instead of TCP/IP.\n" \
  "url=<URL>       (required) The disk image URL to serve.\n" \
  "user=<USER>                The user to log in as.\n" \
  "user-agent=<USER-AGENT>    Send user-agent header for HTTP/HTTPS."

/* Translate CURLcode to nbdkit_error. */
#define display_curl_error(ch, r, fs, ...)                      \
  do {                                                          \
    nbdkit_error ((fs ": %s: %s"), ## __VA_ARGS__,              \
                  curl_easy_strerror ((r)), (ch)->errbuf);      \
  } while (0)

/* Create the per-connection handle. */
static void *
curl_open (int readonly)
{
  struct handle *h;

  h = calloc (1, sizeof *h);
  if (h == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }
  h->readonly = readonly;

  return h;
}

/* Free up the per-connection handle. */
static void
curl_close (void *handle)
{
  struct handle *h = handle;

  free (h);
}

/* This plugin could support the parallel thread model.  It currently
 * uses serialize_requests because parallel has the unfortunate effect
 * of pessimising common workloads.  See:
 * https://listman.redhat.com/archives/libguestfs/2023-February/030618.html
 */
#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_REQUESTS

/* Calls get_handle() ... put_handle() to get a handle for the length
 * of the current scope.
 */
#define GET_HANDLE_FOR_CURRENT_SCOPE(ch) \
  CLEANUP_PUT_HANDLE struct curl_handle *ch = get_handle ();
#define CLEANUP_PUT_HANDLE __attribute__ ((cleanup (cleanup_put_handle)))
static void
cleanup_put_handle (void *chp)
{
  struct curl_handle *ch = * (struct curl_handle **) chp;

  if (ch != NULL)
    put_handle (ch);
}

/* Get the file size. */
static int64_t
curl_get_size (void *handle)
{
  GET_HANDLE_FOR_CURRENT_SCOPE (ch);
  if (ch == NULL)
    return -1;

  return ch->exportsize;
}

/* Multi-conn is safe for read-only connections, but HTTP does not
 * have any concept of flushing so we cannot use it for read-write
 * connections.
 */
static int
curl_can_multi_conn (void *handle)
{
  struct handle *h = handle;

  return !! h->readonly;
}

/* Read data from the remote server. */
static int
curl_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  CURLcode r;
  char range[128];

  GET_HANDLE_FOR_CURRENT_SCOPE (ch);
  if (ch == NULL)
    return -1;

  /* Run the scripts if necessary and set headers in the handle. */
  if (do_scripts (ch) == -1) return -1;

  /* Tell the write_cb where we want the data to be written.  write_cb
   * will update this if the data comes in multiple sections.
   */
  ch->write_buf = buf;
  ch->write_count = count;

  curl_easy_setopt (ch->c, CURLOPT_HTTPGET, 1L);

  /* Make an HTTP range request. */
  snprintf (range, sizeof range, "%" PRIu64 "-%" PRIu64,
            offset, offset + count);
  curl_easy_setopt (ch->c, CURLOPT_RANGE, range);

  /* The assumption here is that curl will look after timeouts. */
  r = curl_easy_perform (ch->c);
  if (r != CURLE_OK) {
    display_curl_error (ch, r, "pread: curl_easy_perform");
    return -1;
  }

  /* Could use curl_easy_getinfo here to obtain further information
   * about the connection.
   */

  /* As far as I understand the cURL API, this should never happen. */
  assert (ch->write_count == 0);

  return 0;
}

/* Write data to the remote server. */
static int
curl_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset)
{
  CURLcode r;
  char range[128];

  GET_HANDLE_FOR_CURRENT_SCOPE (ch);
  if (ch == NULL)
    return -1;

  /* Run the scripts if necessary and set headers in the handle. */
  if (do_scripts (ch) == -1) return -1;

  /* Tell the read_cb where we want the data to be read from.  read_cb
   * will update this if the data comes in multiple sections.
   */
  ch->read_buf = buf;
  ch->read_count = count;

  curl_easy_setopt (ch->c, CURLOPT_UPLOAD, 1L);

  /* Make an HTTP range request. */
  snprintf (range, sizeof range, "%" PRIu64 "-%" PRIu64,
            offset, offset + count);
  curl_easy_setopt (ch->c, CURLOPT_RANGE, range);

  /* The assumption here is that curl will look after timeouts. */
  r = curl_easy_perform (ch->c);
  if (r != CURLE_OK) {
    display_curl_error (ch, r, "pwrite: curl_easy_perform");
    return -1;
  }

  /* Could use curl_easy_getinfo here to obtain further information
   * about the connection.
   */

  /* As far as I understand the cURL API, this should never happen. */
  assert (ch->read_count == 0);

  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "curl",
  .version           = PACKAGE_VERSION,
  .load              = curl_load,
  .unload            = curl_unload,
  .config            = curl_config,
  .config_complete   = curl_config_complete,
  .config_help       = curl_config_help,
  .magic_config_key  = "url",
  .open              = curl_open,
  .close             = curl_close,
  .get_size          = curl_get_size,
  .can_multi_conn    = curl_can_multi_conn,
  .pread             = curl_pread,
  .pwrite            = curl_pwrite,
};

NBDKIT_REGISTER_PLUGIN (plugin)
