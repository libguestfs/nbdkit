/* nbdkit
 * Copyright (C) 2018-2020 Red Hat Inc.
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

/* See web-server.h */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <pthread.h>

#include "web-server.h"

#ifndef SOCK_CLOEXEC
/* For this file, we don't care if fds are marked cloexec; leaking is okay.  */
#define SOCK_CLOEXEC 0
#endif

enum method { HEAD, GET };

static char tmpdir[]   = "/tmp/wsXXXXXX";
static char sockpath[] = "............./sock";
static int listen_sock = -1;
static int fd = -1;
static struct stat statbuf;
static char request[16384];
static check_request_t check_request;

static void *start_web_server (void *arg);
static void handle_requests (int s);
static void handle_file_request (int s, enum method method);
static void handle_mirror_redirect_request (int s);
static void handle_mirror_data_request (int s, enum method method, char byte);
static void send_404_not_found (int s);
static void send_405_method_not_allowed (int s);
static void send_500_internal_server_error (int s);
static void xwrite (int s, const char *buf, size_t len);
static void xpread (char *buf, size_t count, off_t offset);

static void
cleanup (void)
{
  if (fd >= 0)
    close (fd);
  if (listen_sock >= 0)
    close (listen_sock);
  unlink (sockpath);
  rmdir (tmpdir);
}

const char *
web_server (const char *filename, check_request_t _check_request)
{
  struct sockaddr_un addr;
  pthread_t thread;
  int err;

  check_request = _check_request;

  /* Open the file. */
  fd = open (filename, O_RDONLY|O_CLOEXEC);
  if (fd == -1) {
    perror (filename);
    return NULL;
  }
  if (fstat (fd, &statbuf) == -1) {
    perror ("stat");
    goto err1;
  }

  /* Create the temporary directory for the socket. */
  if (mkdtemp (tmpdir) == NULL) {
    perror ("mkdtemp");
    goto err1;
  }

  /* Create the listening socket for the web server. */
  memcpy (sockpath, tmpdir, strlen (tmpdir));
  listen_sock = socket (AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
  if (listen_sock == -1) {
    perror ("socket");
    goto err2;
  }

  addr.sun_family = AF_UNIX;
  memcpy (addr.sun_path, sockpath, strlen (sockpath) + 1);
  if (bind (listen_sock, (struct sockaddr *) &addr, sizeof addr) == -1) {
    perror (sockpath);
    goto err3;
  }

  if (listen (listen_sock, SOMAXCONN) == -1) {
    perror ("listen");
    goto err4;
  }

  /* Run the web server in a separate thread. */
  err = pthread_create (&thread, NULL, start_web_server, NULL);
  if (err) {
    errno = err;
    perror ("pthread_create");
    goto err4;
  }
  err = pthread_detach (thread);
  if (err) {
    errno = err;
    perror ("pthread_detach");
    goto err4;
  }

  atexit (cleanup);

  return sockpath;

 err4:
  unlink (sockpath);
 err3:
  close (listen_sock);
 err2:
  rmdir (tmpdir);
 err1:
  close (fd);
  return NULL;
}

static void *
start_web_server (void *arg)
{
  int s;

  fprintf (stderr, "web server: listening on %s\n", sockpath);

  for (;;) {
    s = accept (listen_sock, NULL, NULL);
    if (s == -1) {
      perror ("web server: accept");
      exit (EXIT_FAILURE);
    }
    handle_requests (s);
  }
}

static void
handle_requests (int s)
{
  bool eof = false;

  fprintf (stderr, "web server: accepted connection\n");

  while (!eof) {
    size_t r, n, sz;
    enum method method;
    char path[128];

    /* Read request until we see "\r\n\r\n" (end of headers) or EOF. */
    n = 0;
    for (;;) {
      if (n >= sizeof request - 1 /* allow one byte for \0 */) {
        fprintf (stderr, "web server: request too long\n");
        exit (EXIT_FAILURE);
      }
      sz = sizeof request - n - 1;
      r = read (s, &request[n], sz);
      if (r == -1) {
        perror ("read");
        exit (EXIT_FAILURE);
      }
      if (r == 0) {
        eof = true;
        break;
      }
      n += r;
      request[n] = '\0';
      if (strstr (request, "\r\n\r\n"))
        break;
    }

    if (n == 0)
      continue;

    fprintf (stderr, "web server: request:\n%s", request);

    /* Call the optional user function to check the request. */
    if (check_request) check_request (request);

    /* Get the method and path fields from the first line. */
    if (strncmp (request, "HEAD ", 5) == 0) {
      method = HEAD;
      n = strcspn (&request[5], " \n\t");
      if (n >= sizeof path) {
        send_500_internal_server_error (s);
        eof = true;
        break;
      }
      memcpy (path, &request[5], n);
      path[n] = '\0';
    }
    else if (strncmp (request, "GET ", 4) == 0) {
      method = GET;
      n = strcspn (&request[4], " \n\t");
      if (n >= sizeof path) {
        send_500_internal_server_error (s);
        eof = true;
        break;
      }
      memcpy (path, &request[4], n);
      path[n] = '\0';
    }
    else {
      send_405_method_not_allowed (s);
      eof = true;
      break;
    }

    fprintf (stderr, "web server: requested path: %s\n", path);

    /* For testing retry-request + curl:
     *   /mirror redirects round-robin to /mirror1, /mirror2, /mirror3
     *   /mirror1 returns a file of \x01 bytes
     *   /mirror2 returns a file of \x02 bytes
     *   /mirror3 returns 404 errors
     * Anything else returns a 500 error
     */
    if (strcmp (path, "/mirror") == 0)
      handle_mirror_redirect_request (s);
    else if (strcmp (path, "/mirror1") == 0)
      handle_mirror_data_request (s, method, 1);
    else if (strcmp (path, "/mirror2") == 0)
      handle_mirror_data_request (s, method, 2);
    else if (strcmp (path, "/mirror3") == 0) {
      send_404_not_found (s);
      eof = true;
    }
    else if (strncmp (path, "/mirror", 7) == 0) {
      send_500_internal_server_error (s);
      eof = true;
    }

    /* Otherwise it's a regular file request.  'path' is ignored, we
     * only serve a single file passed to web_server().
     */
    else
      handle_file_request (s, method);
  }

  close (s);
}

static void
handle_file_request (int s, enum method method)
{
  const bool headers_only = method == HEAD;
  uint64_t offset, length, end;
  const char *p;
  const char response1_ok[] = "HTTP/1.1 200 OK\r\n";
  const char response1_partial[] = "HTTP/1.1 206 Partial Content\r\n";
  const char response2[] =
    "Accept-rANGES:     bytes\r\n" /* See RHBZ#1837337 */
    "Connection: keep-alive\r\n"
    "Content-Type: application/octet-stream\r\n";
  char response3[64];
  const char response4[] = "\r\n";
  char *data;

  /* If there's no Range request header then send the full size as the
   * content-length.
   */
  p = strcasestr (request, "\r\nRange: bytes=");
  if (p == NULL) {
    offset = 0;
    length = statbuf.st_size;
    xwrite (s, response1_ok, strlen (response1_ok));
  }
  else {
    p += 15;
    if (sscanf (p, "%" SCNu64 "-%" SCNu64, &offset, &end) != 2) {
      fprintf (stderr, "web server: could not parse "
               "range request from curl client\n");
      exit (EXIT_FAILURE);
    }
    /* Unclear but "Range: bytes=0-4" means bytes 0-3.  '4' is the
     * byte beyond the end of the range.
     */
    length = end - offset;
    xwrite (s, response1_partial, strlen (response1_partial));
  }

  xwrite (s, response2, strlen (response2));
  snprintf (response3, sizeof response3,
            "Content-Length: %" PRIu64 "\r\n", length);
  xwrite (s, response3, strlen (response3));
  xwrite (s, response4, strlen (response4));

  if (headers_only)
    return;

  /* Send the file content. */
  data = malloc (length);
  if (data == NULL) {
    perror ("malloc");
    exit (EXIT_FAILURE);
  }

  xpread (data, length, offset);
  xwrite (s, data, length);

  free (data);
}

/* Request for /mirror */
static void
handle_mirror_redirect_request (int s)
{
  static char rr = '1';         /* round robin '1', '2', '3' */
  /* Note we send 302 (temporary redirect), same as Fedora's mirrorservice. */
  const char found[] = "HTTP/1.1 302 Found\r\nContent-Length: 0\r\n";
  char location[] = "Location: /mirrorX\r\n";
  const char eol[] = "\r\n";

  location[17] = rr;
  rr++;
  if (rr == '4')
    rr = '1';

  xwrite (s, found, strlen (found));
  xwrite (s, location, strlen (location));
  xwrite (s, eol, strlen (eol));
}

static void
handle_mirror_data_request (int s, enum method method, char byte)
{
  const bool headers_only = method == HEAD;
  uint64_t offset, length, end;
  const char *p;
  const char response1_ok[] = "HTTP/1.1 200 OK\r\n";
  const char response1_partial[] = "HTTP/1.1 206 Partial Content\r\n";
  const char response2[] =
    "Accept-rANGES:     bytes\r\n" /* See RHBZ#1837337 */
    "Connection: keep-alive\r\n"
    "Content-Type: application/octet-stream\r\n";
  char response3[64];
  const char response4[] = "\r\n";
  char *data;

  /* If there's no Range request header then send the full size as the
   * content-length.
   */
  p = strcasestr (request, "\r\nRange: bytes=");
  if (p == NULL) {
    offset = 0;
    length = statbuf.st_size;
    xwrite (s, response1_ok, strlen (response1_ok));
  }
  else {
    p += 15;
    if (sscanf (p, "%" SCNu64 "-%" SCNu64, &offset, &end) != 2) {
      fprintf (stderr, "web server: could not parse "
               "range request from curl client\n");
      exit (EXIT_FAILURE);
    }
    /* Unclear but "Range: bytes=0-4" means bytes 0-3.  '4' is the
     * byte beyond the end of the range.
     */
    length = end - offset;
    xwrite (s, response1_partial, strlen (response1_partial));
  }

  xwrite (s, response2, strlen (response2));
  snprintf (response3, sizeof response3,
            "Content-Length: %" PRIu64 "\r\n", length);
  xwrite (s, response3, strlen (response3));
  xwrite (s, response4, strlen (response4));

  if (headers_only)
    return;

  /* Send the file content. */
  data = malloc (length);
  if (data == NULL) {
    perror ("malloc");
    exit (EXIT_FAILURE);
  }

  memset (data, byte, length);
  xwrite (s, data, length);

  free (data);
}

static void
send_404_not_found (int s)
{
  const char response[] =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Length: 0\r\n"
    "Connection: close\r\n"
    "\r\n";
  xwrite (s, response, strlen (response));
}

static void
send_405_method_not_allowed (int s)
{
  const char response[] =
    "HTTP/1.1 405 Method Not Allowed\r\n"
    "Content-Length: 0\r\n"
    "Connection: close\r\n"
    "\r\n";
  xwrite (s, response, strlen (response));
}

static void
send_500_internal_server_error (int s)
{
  const char response[] =
    "HTTP/1.1 500 Internal Server Error\r\n"
    "Content-Length: 0\r\n"
    "Connection: close\r\n"
    "\r\n";
  xwrite (s, response, strlen (response));
}

static void
xwrite (int s, const char *buf, size_t len)
{
  ssize_t r;

  while (len > 0) {
    r = write (s, buf, len);
    if (r == -1) {
      perror ("write");
      exit (EXIT_FAILURE);
    }
    buf += r;
    len -= r;
  }
}

static void
xpread (char *buf, size_t count, off_t offset)
{
  ssize_t r;

  while (count > 0) {
    r = pread (fd, buf, count, offset);
    if (r == -1) {
      perror ("read");
      exit (EXIT_FAILURE);
    }
    if (r == 0) {
      fprintf (stderr, "pread: unexpected end of file\n");
      exit (EXIT_FAILURE);
    }
    buf += r;
    count -= r;
    offset += r;
  }
}
