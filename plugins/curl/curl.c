/* nbdkit
 * Copyright (C) 2014-2020 Red Hat Inc.
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
#include "ascii-ctype.h"
#include "ascii-string.h"

static const char *url = NULL;  /* required */

static const char *cainfo = NULL;
static const char *capath = NULL;
static char *cookie = NULL;
static struct curl_slist *headers = NULL;
static char *password = NULL;
static long protocols = CURLPROTO_ALL;
static const char *proxy = NULL;
static char *proxy_password = NULL;
static const char *proxy_user = NULL;
static bool sslverify = true;
static bool tcp_keepalive = false;
static bool tcp_nodelay = true;
static uint32_t timeout = 0;
static const char *unix_socket_path = NULL;
static const char *user = NULL;
static const char *user_agent = NULL;

/* Use '-D curl.verbose=1' to set. */
int curl_debug_verbose = 0;

/* Macro CURL_AT_LEAST_VERSION was added in 2015 (Curl 7.43) so if the
 * macro isn't present then Curl is very old.
 */
#ifdef CURL_AT_LEAST_VERSION
#if CURL_AT_LEAST_VERSION(7, 55, 0)
#define HAVE_CURLINFO_CONTENT_LENGTH_DOWNLOAD_T
#endif
#endif

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
  free (password);
  free (proxy_password);
  free (cookie);
  if (headers)
    curl_slist_free_all (headers);
  curl_global_cleanup ();
}

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

  else if (strcmp (key, "cookie") == 0) {
    free (cookie);
    if (nbdkit_read_password (value, &cookie) == -1)
      return -1;
  }

  else if (strcmp (key, "header") == 0) {
    headers = curl_slist_append (headers, value);
    if (headers == NULL) {
      nbdkit_error ("curl_slist_append: %m");
      return -1;
    }
  }

  else if (strcmp (key, "password") == 0) {
    free (password);
    if (nbdkit_read_password (value, &password) == -1)
      return -1;
  }

  else if (strcmp (key, "protocols") == 0) {
    if (parse_protocols (value) == -1)
      return -1;
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

  return 0;
}

#define curl_config_help \
  "cainfo=<CAINFO>            Path to Certificate Authority file.\n" \
  "capath=<CAPATH>            Path to directory with CA certificates.\n" \
  "cookie=<COOKIE>            Set HTTP/HTTPS cookies.\n" \
  "header=<HEADER>            Set HTTP/HTTPS header.\n" \
  "password=<PASSWORD>        The password for the user account.\n" \
  "protocols=PROTO,PROTO,..   Limit protocols allowed.\n" \
  "proxy=<PROXY>              Set proxy URL.\n" \
  "proxy-password=<PASSWORD>  The proxy password.\n" \
  "proxy-user=<USER>          The proxy user.\n" \
  "timeout=<TIMEOUT>          Set the timeout for requests (seconds).\n" \
  "sslverify=false            Do not verify SSL certificate of remote host.\n" \
  "tcp-keepalive=true         Enable TCP keepalives.\n" \
  "tcp-nodelay=false          Disable Nagle’s algorithm.\n" \
  "unix-socket-path=<PATH>    Open Unix domain socket instead of TCP/IP.\n" \
  "url=<URL>       (required) The disk image URL to serve.\n" \
  "user=<USER>                The user to log in as.\n" \
  "user-agent=<USER-AGENT>    Send user-agent header for HTTP/HTTPS."

/* The per-connection handle. */
struct curl_handle {
  CURL *c;
  bool accept_range;
  int64_t exportsize;
  char errbuf[CURL_ERROR_SIZE];
  char *write_buf;
  uint32_t write_count;
  const char *read_buf;
  uint32_t read_count;
};

/* Translate CURLcode to nbdkit_error. */
#define display_curl_error(h, r, fs, ...)                       \
  do {                                                          \
    nbdkit_error ((fs ": %s: %s"), ## __VA_ARGS__,              \
                  curl_easy_strerror ((r)), (h)->errbuf);       \
  } while (0)

static int debug_cb (CURL *handle, curl_infotype type,
                     const char *data, size_t size, void *);
static size_t header_cb (void *ptr, size_t size, size_t nmemb, void *opaque);
static size_t write_cb (char *ptr, size_t size, size_t nmemb, void *opaque);
static size_t read_cb (void *ptr, size_t size, size_t nmemb, void *opaque);

/* Create the per-connection handle. */
static void *
curl_open (int readonly)
{
  struct curl_handle *h;
  CURLcode r;
#ifdef HAVE_CURLINFO_CONTENT_LENGTH_DOWNLOAD_T
  curl_off_t o;
#else
  double d;
#endif

  h = calloc (1, sizeof *h);
  if (h == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }

  h->c = curl_easy_init ();
  if (h->c == NULL) {
    nbdkit_error ("curl_easy_init: failed: %m");
    goto err;
  }

  if (curl_debug_verbose) {
    /* NB: Constants must be explicitly long because the parameter is
     * varargs.
     */
    curl_easy_setopt (h->c, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt (h->c, CURLOPT_DEBUGFUNCTION, debug_cb);
  }

  curl_easy_setopt (h->c, CURLOPT_ERRORBUFFER, h->errbuf);

  r = CURLE_OK;
  if (unix_socket_path) {
#if HAVE_CURLOPT_UNIX_SOCKET_PATH
    r = curl_easy_setopt (h->c, CURLOPT_UNIX_SOCKET_PATH, unix_socket_path);
#else
    r = CURLE_UNKNOWN_OPTION;
#endif
  }
  if (r != CURLE_OK) {
    display_curl_error (h, r, "curl_easy_setopt: CURLOPT_UNIX_SOCKET_PATH");
    goto err;
  }

  /* Set the URL. */
  r = curl_easy_setopt (h->c, CURLOPT_URL, url);
  if (r != CURLE_OK) {
    display_curl_error (h, r, "curl_easy_setopt: CURLOPT_URL [%s]", url);
    goto err;
  }

  /* Various options we always set. */
  curl_easy_setopt (h->c, CURLOPT_AUTOREFERER, 1);
  curl_easy_setopt (h->c, CURLOPT_FOLLOWLOCATION, 1);
  curl_easy_setopt (h->c, CURLOPT_FAILONERROR, 1);

  /* Options. */
  if (cainfo)
    curl_easy_setopt (h->c, CURLOPT_CAINFO, cainfo);
  if (capath)
    curl_easy_setopt (h->c, CURLOPT_CAPATH, capath);
  if (cookie)
    curl_easy_setopt (h->c, CURLOPT_COOKIE, cookie);
  if (headers)
    curl_easy_setopt (h->c, CURLOPT_HTTPHEADER, headers);
  if (password)
    curl_easy_setopt (h->c, CURLOPT_PASSWORD, password);
  if (protocols != CURLPROTO_ALL) {
    curl_easy_setopt (h->c, CURLOPT_PROTOCOLS, protocols);
    curl_easy_setopt (h->c, CURLOPT_REDIR_PROTOCOLS, protocols);
  }
  if (proxy)
    curl_easy_setopt (h->c, CURLOPT_PROXY, proxy);
  if (proxy_password)
    curl_easy_setopt (h->c, CURLOPT_PROXYPASSWORD, proxy_password);
  if (proxy_user)
    curl_easy_setopt (h->c, CURLOPT_PROXYUSERNAME, proxy_user);
  if (!sslverify) {
    /* NB: Constants must be explicitly long because the parameter is
     * varargs.
     */
    curl_easy_setopt (h->c, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt (h->c, CURLOPT_SSL_VERIFYHOST, 0L);
  }
  if (tcp_keepalive)
    curl_easy_setopt (h->c, CURLOPT_TCP_KEEPALIVE, 1L);
  if (!tcp_nodelay)
    curl_easy_setopt (h->c, CURLOPT_TCP_NODELAY, 0L);
  if (timeout > 0)
    /* NB: The cast is required here because the parameter is varargs
     * treated as long, and not type safe.
     */
    curl_easy_setopt (h->c, CURLOPT_TIMEOUT, (long) timeout);
  if (user)
    curl_easy_setopt (h->c, CURLOPT_USERNAME, user);
  if (user_agent)
    curl_easy_setopt (h->c, CURLOPT_USERAGENT, user_agent);

  /* Get the file size and also whether the remote HTTP server
   * supports byte ranges.
   */
  h->accept_range = false;
  curl_easy_setopt (h->c, CURLOPT_NOBODY, 1); /* No Body, not nobody! */
  curl_easy_setopt (h->c, CURLOPT_HEADERFUNCTION, header_cb);
  curl_easy_setopt (h->c, CURLOPT_HEADERDATA, h);
  r = curl_easy_perform (h->c);
  if (r != CURLE_OK) {
    display_curl_error (h, r,
                        "problem doing HEAD request to fetch size of URL [%s]",
                        url);
    goto err;
  }

#ifdef HAVE_CURLINFO_CONTENT_LENGTH_DOWNLOAD_T
  r = curl_easy_getinfo (h->c, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &o);
  if (r != CURLE_OK) {
    display_curl_error (h, r, "could not get length of remote file [%s]", url);
    goto err;
  }

  if (o == -1) {
    nbdkit_error ("could not get length of remote file [%s], "
                  "is the URL correct?", url);
    goto err;
  }

  h->exportsize = o;
#else
  r = curl_easy_getinfo (h->c, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &d);
  if (r != CURLE_OK) {
    display_curl_error (h, r, "could not get length of remote file [%s]", url);
    goto err;
  }

  if (d == -1) {
    nbdkit_error ("could not get length of remote file [%s], "
                  "is the URL correct?", url);
    goto err;
  }

  h->exportsize = d;
#endif
  nbdkit_debug ("content length: %" PRIi64, h->exportsize);

  if (ascii_strncasecmp (url, "http://", strlen ("http://")) == 0 ||
      ascii_strncasecmp (url, "https://", strlen ("https://")) == 0) {
    if (!h->accept_range) {
      nbdkit_error ("server does not support 'range' (byte range) requests");
      goto err;
    }

    nbdkit_debug ("accept range supported (for HTTP/HTTPS)");
  }

  /* Get set up for reading and writing. */
  curl_easy_setopt (h->c, CURLOPT_HEADERFUNCTION, NULL);
  curl_easy_setopt (h->c, CURLOPT_HEADERDATA, NULL);
  curl_easy_setopt (h->c, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt (h->c, CURLOPT_WRITEDATA, h);
  if (!readonly) {
    curl_easy_setopt (h->c, CURLOPT_READFUNCTION, read_cb);
    curl_easy_setopt (h->c, CURLOPT_READDATA, h);
  }

  return h;

 err:
  if (h->c)
    curl_easy_cleanup (h->c);
  free (h);
  return NULL;
}

/* When using CURLOPT_VERBOSE, this callback is used to redirect
 * messages to nbdkit_debug (instead of stderr).
 */
static int
debug_cb (CURL *handle, curl_infotype type,
          const char *data, size_t size, void *opaque)
{
  size_t origsize = size;
  CLEANUP_FREE char *str;

  /* The data parameter passed is NOT \0-terminated, but also it may
   * have \n or \r\n line endings.  The only sane way to deal with
   * this is to copy the string.  (The data strings may also be
   * multi-line, but we don't deal with that here).
   */
  str = malloc (size + 1);
  if (str == NULL)
    goto out;
  memcpy (str, data, size);
  str[size] = '\0';

  while (size > 0 && (str[size-1] == '\n' || str[size-1] == '\r')) {
    str[size-1] = '\0';
    size--;
  }

  switch (type) {
  case CURLINFO_TEXT:
    nbdkit_debug ("%s", str);
    break;
  case CURLINFO_HEADER_IN:
    nbdkit_debug ("S: %s", str);
    break;
  case CURLINFO_HEADER_OUT:
    nbdkit_debug ("C: %s", str);
    break;
  default:
    /* Assume everything else is binary data that we cannot print. */
    nbdkit_debug ("<data with size=%zu>", origsize);
  }

 out:
  return 0;
}

static size_t
header_cb (void *ptr, size_t size, size_t nmemb, void *opaque)
{
  struct curl_handle *h = opaque;
  size_t realsize = size * nmemb;
  const char *header = ptr;
  const char *end = header + realsize;
  const char *accept_ranges = "accept-ranges:";
  const char *bytes = "bytes";

  if (realsize >= strlen (accept_ranges) &&
      ascii_strncasecmp (header, accept_ranges, strlen (accept_ranges)) == 0) {
    const char *p = strchr (header, ':') + 1;

    /* Skip whitespace between the header name and value. */
    while (p < end && *p && ascii_isspace (*p))
      p++;

    if (end - p >= strlen (bytes)
        && strncmp (p, bytes, strlen (bytes)) == 0) {
      /* Check that there is nothing but whitespace after the value. */
      p += strlen (bytes);
      while (p < end && *p && ascii_isspace (*p))
        p++;

      if (p == end || !*p)
        h->accept_range = true;
    }
  }

  return realsize;
}

/* Free up the per-connection handle. */
static void
curl_close (void *handle)
{
  struct curl_handle *h = handle;

  curl_easy_cleanup (h->c);
  free (h);
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_REQUESTS

/* Get the file size. */
static int64_t
curl_get_size (void *handle)
{
  struct curl_handle *h = handle;

  return h->exportsize;
}

/* NB: The terminology used by libcurl is confusing!
 *
 * WRITEFUNCTION / write_cb is used when reading from the remote server
 * READFUNCTION / read_cb is used when writing to the remote server.
 *
 * We use the same terminology as libcurl here.
 */

/* Read data from the remote server. */
static int
curl_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  struct curl_handle *h = handle;
  CURLcode r;
  char range[128];

  /* Tell the write_cb where we want the data to be written.  write_cb
   * will update this if the data comes in multiple sections.
   */
  h->write_buf = buf;
  h->write_count = count;

  curl_easy_setopt (h->c, CURLOPT_HTTPGET, 1);

  /* Make an HTTP range request. */
  snprintf (range, sizeof range, "%" PRIu64 "-%" PRIu64,
            offset, offset + count);
  curl_easy_setopt (h->c, CURLOPT_RANGE, range);

  /* The assumption here is that curl will look after timeouts. */
  r = curl_easy_perform (h->c);
  if (r != CURLE_OK) {
    display_curl_error (h, r, "pread: curl_easy_perform");
    return -1;
  }

  /* Could use curl_easy_getinfo here to obtain further information
   * about the connection.
   */

  /* As far as I understand the cURL API, this should never happen. */
  assert (h->write_count == 0);

  return 0;
}

static size_t
write_cb (char *ptr, size_t size, size_t nmemb, void *opaque)
{
  struct curl_handle *h = opaque;
  size_t orig_realsize = size * nmemb;
  size_t realsize = orig_realsize;

  assert (h->write_buf);

  /* Don't read more than the requested amount of data, even if the
   * server or libcurl sends more.
   */
  if (realsize > h->write_count)
    realsize = h->write_count;

  memcpy (h->write_buf, ptr, realsize);

  h->write_count -= realsize;
  h->write_buf += realsize;

  return orig_realsize;
}

/* Write data to the remote server. */
static int
curl_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset)
{
  struct curl_handle *h = handle;
  CURLcode r;
  char range[128];

  /* Tell the read_cb where we want the data to be read from.  read_cb
   * will update this if the data comes in multiple sections.
   */
  h->read_buf = buf;
  h->read_count = count;

  curl_easy_setopt (h->c, CURLOPT_UPLOAD, 1);

  /* Make an HTTP range request. */
  snprintf (range, sizeof range, "%" PRIu64 "-%" PRIu64,
            offset, offset + count);
  curl_easy_setopt (h->c, CURLOPT_RANGE, range);

  /* The assumption here is that curl will look after timeouts. */
  r = curl_easy_perform (h->c);
  if (r != CURLE_OK) {
    display_curl_error (h, r, "pwrite: curl_easy_perform");
    return -1;
  }

  /* Could use curl_easy_getinfo here to obtain further information
   * about the connection.
   */

  /* As far as I understand the cURL API, this should never happen. */
  assert (h->read_count == 0);

  return 0;
}

static size_t
read_cb (void *ptr, size_t size, size_t nmemb, void *opaque)
{
  struct curl_handle *h = opaque;
  size_t realsize = size * nmemb;

  assert (h->read_buf);
  if (realsize > h->read_count)
    realsize = h->read_count;

  memcpy (ptr, h->read_buf, realsize);

  h->read_count -= realsize;
  h->read_buf += realsize;

  return realsize;
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
  .pread             = curl_pread,
  .pwrite            = curl_pwrite,
};

NBDKIT_REGISTER_PLUGIN(plugin)
