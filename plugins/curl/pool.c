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

/* Curl handle pool.
 *
 * To get a libcurl handle, call get_handle().  When you hold the
 * handle, it is yours exclusively to use.  After you have finished
 * with the handle, put it back into the pool by calling put_handle().
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include <curl/curl.h>

#include <nbdkit-plugin.h>

#include "ascii-ctype.h"
#include "ascii-string.h"
#include "cleanup.h"
#include "vector.h"

#include "curldefs.h"

/* Translate CURLcode to nbdkit_error. */
#define display_curl_error(ch, r, fs, ...)                      \
  do {                                                          \
    nbdkit_error ((fs ": %s: %s"), ## __VA_ARGS__,              \
                  curl_easy_strerror ((r)), (ch)->errbuf);      \
  } while (0)

static struct curl_handle *allocate_handle (void);
static void free_handle (struct curl_handle *);
static int debug_cb (CURL *handle, curl_infotype type,
                     const char *data, size_t size, void *);
static size_t header_cb (void *ptr, size_t size, size_t nmemb, void *opaque);
static size_t write_cb (char *ptr, size_t size, size_t nmemb, void *opaque);
static size_t read_cb (void *ptr, size_t size, size_t nmemb, void *opaque);

/* This lock protects access to the curl_handles vector below. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* List of curl handles.  This is allocated dynamically as more
 * handles are requested.  Currently it does not shrink.  It may grow
 * up to 'connections' in length.
 */
DEFINE_VECTOR_TYPE(curl_handle_list, struct curl_handle *)
static curl_handle_list curl_handles = empty_vector;

/* The condition is used when the curl handles vector is full and
 * we're waiting for a thread to put_handle.
 */
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static size_t in_use = 0, waiting = 0;

/* Close and free all handles in the pool. */
void
free_all_handles (void)
{
  size_t i;

  nbdkit_debug ("free_all_handles: number of curl handles allocated: %zu",
                curl_handles.len);

  for (i = 0; i < curl_handles.len; ++i)
    free_handle (curl_handles.ptr[i]);
  curl_handle_list_reset (&curl_handles);
}

/* Get a handle from the pool.
 *
 * It is owned exclusively by the caller until they call put_handle.
 */
struct curl_handle *
get_handle (void)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
  size_t i;
  struct curl_handle *ch;

 again:
  /* Look for a handle which is not in_use. */
  for (i = 0; i < curl_handles.len; ++i) {
    ch = curl_handles.ptr[i];
    if (!ch->in_use) {
      ch->in_use = true;
      in_use++;
      return ch;
    }
  }

  /* If more connections are allowed, then allocate a new handle. */
  if (curl_handles.len < connections) {
    ch = allocate_handle ();
    if (ch == NULL)
      return NULL;
    if (curl_handle_list_append (&curl_handles, ch) == -1) {
      free_handle (ch);
      return NULL;
    }
    ch->in_use = true;
    in_use++;
    return ch;
  }

  /* Otherwise we have run out of connections so we must wait until
   * another thread calls put_handle.
   */
  assert (in_use == connections);
  waiting++;
  while (in_use == connections)
    pthread_cond_wait (&cond, &lock);
  waiting--;

  goto again;
}

/* Return the handle to the pool. */
void
put_handle (struct curl_handle *ch)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);

  ch->in_use = false;
  in_use--;

  /* Signal the next thread which is waiting. */
  if (waiting > 0)
    pthread_cond_signal (&cond);
}

/* Allocate and initialize a new libcurl handle. */
static struct curl_handle *
allocate_handle (void)
{
  struct curl_handle *ch;
  CURLcode r;
#ifdef HAVE_CURLINFO_CONTENT_LENGTH_DOWNLOAD_T
  curl_off_t o;
#else
  double d;
#endif

  ch = calloc (1, sizeof *ch);
  if (ch == NULL) {
    nbdkit_error ("calloc: %m");
    free (ch);
    return NULL;
  }

  ch->c = curl_easy_init ();
  if (ch->c == NULL) {
    nbdkit_error ("curl_easy_init: failed: %m");
    goto err;
  }

  if (curl_debug_verbose) {
    /* NB: Constants must be explicitly long because the parameter is
     * varargs.
     */
    curl_easy_setopt (ch->c, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt (ch->c, CURLOPT_DEBUGFUNCTION, debug_cb);
  }

  curl_easy_setopt (ch->c, CURLOPT_ERRORBUFFER, ch->errbuf);

  r = CURLE_OK;
  if (unix_socket_path) {
#if HAVE_CURLOPT_UNIX_SOCKET_PATH
    r = curl_easy_setopt (ch->c, CURLOPT_UNIX_SOCKET_PATH, unix_socket_path);
#else
    r = CURLE_UNKNOWN_OPTION;
#endif
  }
  if (r != CURLE_OK) {
    display_curl_error (ch, r, "curl_easy_setopt: CURLOPT_UNIX_SOCKET_PATH");
    goto err;
  }

  /* Set the URL. */
  r = curl_easy_setopt (ch->c, CURLOPT_URL, url);
  if (r != CURLE_OK) {
    display_curl_error (ch, r, "curl_easy_setopt: CURLOPT_URL [%s]", url);
    goto err;
  }

  /* Various options we always set.
   *
   * NB: Both here and below constants must be explicitly long because
   * the parameter is varargs.
   *
   * For use of CURLOPT_NOSIGNAL see:
   * https://curl.se/libcurl/c/CURLOPT_NOSIGNAL.html
   */
  curl_easy_setopt (ch->c, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt (ch->c, CURLOPT_AUTOREFERER, 1L);
  if (followlocation)
    curl_easy_setopt (ch->c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt (ch->c, CURLOPT_FAILONERROR, 1L);

  /* Options. */
  if (cainfo) {
    if (strlen (cainfo) == 0)
      curl_easy_setopt (ch->c, CURLOPT_CAINFO, NULL);
    else
      curl_easy_setopt (ch->c, CURLOPT_CAINFO, cainfo);
  }
  if (capath)
    curl_easy_setopt (ch->c, CURLOPT_CAPATH, capath);
  if (cookie)
    curl_easy_setopt (ch->c, CURLOPT_COOKIE, cookie);
  if (cookiefile)
    curl_easy_setopt (ch->c, CURLOPT_COOKIEFILE, cookiefile);
  if (cookiejar)
    curl_easy_setopt (ch->c, CURLOPT_COOKIEJAR, cookiejar);
  if (headers)
    curl_easy_setopt (ch->c, CURLOPT_HTTPHEADER, headers);
  if (password)
    curl_easy_setopt (ch->c, CURLOPT_PASSWORD, password);
#ifndef HAVE_CURLOPT_PROTOCOLS_STR
  if (protocols != CURLPROTO_ALL) {
    curl_easy_setopt (ch->c, CURLOPT_PROTOCOLS, protocols);
    curl_easy_setopt (ch->c, CURLOPT_REDIR_PROTOCOLS, protocols);
  }
#else /* HAVE_CURLOPT_PROTOCOLS_STR (new in 7.85.0) */
  if (protocols) {
    curl_easy_setopt (ch->c, CURLOPT_PROTOCOLS_STR, protocols);
    curl_easy_setopt (ch->c, CURLOPT_REDIR_PROTOCOLS_STR, protocols);
  }
#endif /* HAVE_CURLOPT_PROTOCOLS_STR */
  if (proxy)
    curl_easy_setopt (ch->c, CURLOPT_PROXY, proxy);
  if (proxy_password)
    curl_easy_setopt (ch->c, CURLOPT_PROXYPASSWORD, proxy_password);
  if (proxy_user)
    curl_easy_setopt (ch->c, CURLOPT_PROXYUSERNAME, proxy_user);
  if (!sslverify) {
    curl_easy_setopt (ch->c, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt (ch->c, CURLOPT_SSL_VERIFYHOST, 0L);
  }
  if (ssl_version != CURL_SSLVERSION_DEFAULT)
    curl_easy_setopt (ch->c, CURLOPT_SSLVERSION, (long) ssl_version);
  if (ssl_cipher_list)
    curl_easy_setopt (ch->c, CURLOPT_SSL_CIPHER_LIST, ssl_cipher_list);
  if (tls13_ciphers) {
#if (LIBCURL_VERSION_MAJOR > 7) || \
    (LIBCURL_VERSION_MAJOR == 7 && LIBCURL_VERSION_MINOR >= 61)
    curl_easy_setopt (ch->c, CURLOPT_TLS13_CIPHERS, tls13_ciphers);
#else
    /* This is not available before curl-7.61 */
    nbdkit_error ("tls13-ciphers is not supported in this build of "
                  "nbdkit-curl-plugin");
    goto err;
#endif
  }
  if (tcp_keepalive)
    curl_easy_setopt (ch->c, CURLOPT_TCP_KEEPALIVE, 1L);
  if (!tcp_nodelay)
    curl_easy_setopt (ch->c, CURLOPT_TCP_NODELAY, 0L);
  if (timeout > 0)
    /* NB: The cast is required here because the parameter is varargs
     * treated as long, and not type safe.
     */
    curl_easy_setopt (ch->c, CURLOPT_TIMEOUT, (long) timeout);
  if (user)
    curl_easy_setopt (ch->c, CURLOPT_USERNAME, user);
  if (user_agent)
    curl_easy_setopt (ch->c, CURLOPT_USERAGENT, user_agent);

  /* Get the file size and also whether the remote HTTP server
   * supports byte ranges.
   *
   * We must run the scripts if necessary and set headers in the
   * handle.
   */
  if (do_scripts (ch) == -1) goto err;
  ch->accept_range = false;
  curl_easy_setopt (ch->c, CURLOPT_NOBODY, 1L); /* No Body, not nobody! */
  curl_easy_setopt (ch->c, CURLOPT_HEADERFUNCTION, header_cb);
  curl_easy_setopt (ch->c, CURLOPT_HEADERDATA, ch);
  r = curl_easy_perform (ch->c);
  if (r != CURLE_OK) {
    display_curl_error (ch, r,
                        "problem doing HEAD request to fetch size of URL [%s]",
                        url);
    goto err;
  }

#ifdef HAVE_CURLINFO_CONTENT_LENGTH_DOWNLOAD_T
  r = curl_easy_getinfo (ch->c, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &o);
  if (r != CURLE_OK) {
    display_curl_error (ch, r,
                        "could not get length of remote file [%s]", url);
    goto err;
  }

  if (o == -1) {
    nbdkit_error ("could not get length of remote file [%s], "
                  "is the URL correct?", url);
    goto err;
  }

  ch->exportsize = o;
#else
  r = curl_easy_getinfo (ch->c, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &d);
  if (r != CURLE_OK) {
    display_curl_error (ch, r,
                        "could not get length of remote file [%s]", url);
    goto err;
  }

  if (d == -1) {
    nbdkit_error ("could not get length of remote file [%s], "
                  "is the URL correct?", url);
    goto err;
  }

  ch->exportsize = d;
#endif
  nbdkit_debug ("content length: %" PRIi64, ch->exportsize);

  if (ascii_strncasecmp (url, "http://", strlen ("http://")) == 0 ||
      ascii_strncasecmp (url, "https://", strlen ("https://")) == 0) {
    if (!ch->accept_range) {
      nbdkit_error ("server does not support 'range' (byte range) requests");
      goto err;
    }

    nbdkit_debug ("accept range supported (for HTTP/HTTPS)");
  }

  /* Get set up for reading and writing. */
  curl_easy_setopt (ch->c, CURLOPT_HEADERFUNCTION, NULL);
  curl_easy_setopt (ch->c, CURLOPT_HEADERDATA, NULL);
  curl_easy_setopt (ch->c, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt (ch->c, CURLOPT_WRITEDATA, ch);
  /* These are only used if !readonly but we always register them. */
  curl_easy_setopt (ch->c, CURLOPT_READFUNCTION, read_cb);
  curl_easy_setopt (ch->c, CURLOPT_READDATA, ch);

  return ch;

 err:
  if (ch->c)
    curl_easy_cleanup (ch->c);
  free (ch);
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
  struct curl_handle *ch = opaque;
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
        ch->accept_range = true;
    }
  }

  return realsize;
}

/* NB: The terminology used by libcurl is confusing!
 *
 * WRITEFUNCTION / write_cb is used when reading from the remote server
 * READFUNCTION / read_cb is used when writing to the remote server.
 *
 * We use the same terminology as libcurl here.
 */

static size_t
write_cb (char *ptr, size_t size, size_t nmemb, void *opaque)
{
  struct curl_handle *ch = opaque;
  size_t orig_realsize = size * nmemb;
  size_t realsize = orig_realsize;

  assert (ch->write_buf);

  /* Don't read more than the requested amount of data, even if the
   * server or libcurl sends more.
   */
  if (realsize > ch->write_count)
    realsize = ch->write_count;

  memcpy (ch->write_buf, ptr, realsize);

  ch->write_count -= realsize;
  ch->write_buf += realsize;

  return orig_realsize;
}

static size_t
read_cb (void *ptr, size_t size, size_t nmemb, void *opaque)
{
  struct curl_handle *ch = opaque;
  size_t realsize = size * nmemb;

  assert (ch->read_buf);
  if (realsize > ch->read_count)
    realsize = ch->read_count;

  memcpy (ptr, ch->read_buf, realsize);

  ch->read_count -= realsize;
  ch->read_buf += realsize;

  return realsize;
}

static void
free_handle (struct curl_handle *ch)
{
  curl_easy_cleanup (ch->c);
  if (ch->headers_copy)
    curl_slist_free_all (ch->headers_copy);
  free (ch);
}
