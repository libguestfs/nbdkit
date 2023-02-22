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

#ifndef NBDKIT_CURLDEFS_H
#define NBDKIT_CURLDEFS_H

#include <stdbool.h>

#include "windows-compat.h"

/* Macro CURL_AT_LEAST_VERSION was added in 2015 (Curl 7.43) so if the
 * macro isn't present then Curl is very old.
 */
#ifdef CURL_AT_LEAST_VERSION
#if CURL_AT_LEAST_VERSION(7, 55, 0)
#define HAVE_CURLINFO_CONTENT_LENGTH_DOWNLOAD_T
#endif
#endif

extern const char *url;

extern const char *cainfo;
extern const char *capath;
extern unsigned connections;
extern char *cookie;
extern const char *cookiefile;
extern const char *cookiejar;
extern const char *cookie_script;
extern unsigned cookie_script_renew;
extern bool followlocation;
extern struct curl_slist *headers;
extern const char *header_script;
extern unsigned header_script_renew;
extern long http_version;
extern char *password;
#ifndef HAVE_CURLOPT_PROTOCOLS_STR
extern long protocols;
#else
extern const char *protocols;
#endif
extern const char *proxy;
extern char *proxy_password;
extern const char *proxy_user;
extern bool sslverify;
extern const char *ssl_cipher_list;
extern long ssl_version;
extern const char *tls13_ciphers;
extern bool tcp_keepalive;
extern bool tcp_nodelay;
extern uint32_t timeout;
extern const char *unix_socket_path;
extern const char *user;
extern const char *user_agent;

extern int curl_debug_verbose;

/* The per-connection handle. */
struct handle {
  int readonly;
};

/* The libcurl handle and some associated fields and buffers. */
struct curl_handle {
  /* The underlying curl handle. */
  CURL *c;

  /* True if the handle is in use by a thread. */
  bool in_use;

  /* These fields are used/initialized when we create the handle. */
  bool accept_range;
  int64_t exportsize;

  char errbuf[CURL_ERROR_SIZE];

  /* Before doing a read or write operation, set these to point to the
   * buffer where you want the data to be stored / come from.  Note
   * the confusing terminology from libcurl: write_* is used when
   * reading, read_* is used when writing.
   */
  char *write_buf;
  uint32_t write_count;
  const char *read_buf;
  uint32_t read_count;

  /* Used by scripts.c */
  struct curl_slist *headers_copy;
};

/* pool.c */
extern struct curl_handle *get_handle (void);
extern void put_handle (struct curl_handle *ch);
extern void free_all_handles (void);

/* scripts.c */
extern int do_scripts (struct curl_handle *ch);
extern void scripts_unload (void);

#endif /* NBDKIT_CURLDEFS_H */
