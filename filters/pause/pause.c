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
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <pthread.h>

#include <nbdkit-filter.h>

#include "ascii-ctype.h"
#include "cleanup.h"
#include "utils.h"
#include "unix-path-max.h"

static char *sockfile;
static int sock = -1;

static void
pause_unload (void)
{
  if (sock >= 0)
    close (sock);
  if (sockfile) {
    unlink (sockfile);
    free (sockfile);
  }
}

static int
pause_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
              const char *key, const char *value)
{
  if (strcmp (key, "pause-control") == 0) {
    free (sockfile);
    sockfile = nbdkit_absolute_path (value);
    if (sockfile == NULL)
      return -1;
    return 0;
  }
  else
    return next (nxdata, key, value);
}

static int
pause_config_complete (nbdkit_next_config_complete *next,
                       nbdkit_backend *nxdata)
{
  size_t len;
  struct sockaddr_un addr;

  if (sockfile == NULL) {
    nbdkit_error ("pause-control socket was not set");
    return -1;
  }
  len = strlen (sockfile);
  if (len >= UNIX_PATH_MAX) {
    nbdkit_error ("pause-control socket path too long: "
                  "length %zu > max %d bytes",
                  len, UNIX_PATH_MAX-1);
    return -1;
  }

  /* If the socket already exists, remove it. */
  unlink (sockfile);

#ifdef SOCK_CLOEXEC
  sock = socket (AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
#else
  /* Fortunately, this code is only run at startup, so there is no
   * risk of the fd leaking to a plugin's fork()
   */
  sock = set_cloexec (socket (AF_UNIX, SOCK_STREAM, 0));
#endif
  if (sock == -1) {
    nbdkit_error ("socket: %m");
    return -1;
  }

  addr.sun_family = AF_UNIX;
  memcpy (addr.sun_path, sockfile, len+1 /* trailing \0 */);

  if (bind (sock, (struct sockaddr *) &addr, sizeof addr) == -1) {
    nbdkit_error ("%s: %m", sockfile);
    return -1;
  }

  if (listen (sock, SOMAXCONN) == -1) {
    nbdkit_error ("listen: %m");
    return -1;
  }

  return next (nxdata);
}

#define pause_config_help \
  "pause-control=SOCKET           Control socket."

/* This is locked by the background thread when we're paused, causing
 * all requests in the main threads to hang.
 */
static pthread_mutex_t paused = PTHREAD_MUTEX_INITIALIZER;
static bool is_paused = false;

/* This keeps track of the number of NBD requests in flight. */
static pthread_mutex_t count_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t count_cond = PTHREAD_COND_INITIALIZER;
static unsigned count_requests = 0;

static void
do_pause (void)
{
  if (is_paused) return;

  /* Grabbing the paused lock is enough to stop request processing. */
  pthread_mutex_lock (&paused);
  is_paused = true;

  /* However we must also wait until all outstanding requests have
   * been completed before we send the acknowledgement.
   */
  nbdkit_debug ("pause: pausing, waiting for requests to complete");
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&count_lock);
  while (count_requests > 0)
    pthread_cond_wait (&count_cond, &count_lock);
  nbdkit_debug ("pause: paused");
}

static void
do_resume (void)
{
  if (!is_paused) return;

  /* Release the worker threads. */
  is_paused = false;
  pthread_mutex_unlock (&paused);
  nbdkit_debug ("pause: resumed");
}

/* Background thread which monitors the control socket.  This can only
 * accept one connection at a time.
 */
static void *
control_socket_thread (void *arg)
{
  int s;
  char c;
  ssize_t n;

  for (;;) {
#ifdef HAVE_ACCEPT4
    s = accept4 (sock, NULL, NULL, SOCK_CLOEXEC);
#else
    /* This isn't thread-safe but there's not a lot we can do. */
    s = set_cloexec (accept (sock, NULL, NULL));
#endif
    if (s == -1) goto out;

    /* Read commands (which are single bytes) until end of file. */
    while ((n = read (s, &c, 1)) == 1) {
      switch (c) {
      case 'p':
        do_pause ();
        break;
      case 'r':
        do_resume ();
        break;
      case '\n':
      case '\t':
      case ' ':
        /* For convenience of interactive use, ignore and don't
         * respond to some whitespace characters.
         */
        continue;
      default:          /* Unknown command. */
        c = 'X';
      }
      /* Send the response. */
      c = ascii_toupper (c);
      n = write (s, &c, 1);
      if (n == -1) goto out;
    }
    if (n == 0)
      errno = 0;
  out:
    if (errno && (errno != EINTR && errno != EAGAIN))
      nbdkit_error ("accept: %m");
    if (s >= 0)
      close (s);
  }

  /*NOTREACHED*/
  return NULL;
}

/* Start the background thread after fork. */
static int
pause_after_fork (nbdkit_backend *nxdata)
{
  int err;
  pthread_t thread;

  err = pthread_create (&thread, NULL, control_socket_thread, NULL);
  if (err != 0) {
    errno = err;
    nbdkit_error ("pthread_create: %m");
    return -1;
  }
  return 0;
}

/* This is called before processing each NBD request. */
static void
begin_request (void)
{
  /* This will hang if we're paused. */
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&paused);

  /* Count the number of requests in flight. */
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&count_lock);
  count_requests++;
}

/* This is called after processing each NBD request. */
static void
end_request (void)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&count_lock);
  count_requests--;
  pthread_cond_signal (&count_cond);
}

/* Read data. */
static int
pause_pread (nbdkit_next *next,
             void *handle, void *buf, uint32_t count, uint64_t offset,
             uint32_t flags, int *err)
{
  int r;

  begin_request ();
  r = next->pread (next, buf, count, offset, flags, err);
  end_request ();
  return r;
}

/* Write data. */
static int
pause_pwrite (nbdkit_next *next,
              void *handle,
              const void *buf, uint32_t count, uint64_t offset, uint32_t flags,
              int *err)
{
  int r;

  begin_request ();
  r = next->pwrite (next, buf, count, offset, flags, err);
  end_request ();
  return r;
}

/* Zero data. */
static int
pause_zero (nbdkit_next *next,
            void *handle, uint32_t count, uint64_t offset, uint32_t flags,
            int *err)
{
  int r;

  begin_request ();
  r = next->zero (next, count, offset, flags, err);
  end_request ();
  return r;
}

/* Trim data. */
static int
pause_trim (nbdkit_next *next,
            void *handle, uint32_t count, uint64_t offset,
            uint32_t flags, int *err)
{
  int r;

  begin_request ();
  r = next->trim (next, count, offset, flags, err);
  end_request ();
  return r;
}

/* Extents. */
static int
pause_extents (nbdkit_next *next,
               void *handle, uint32_t count, uint64_t offset, uint32_t flags,
               struct nbdkit_extents *extents, int *err)
{
  int r;

  begin_request ();
  r = next->extents (next, count, offset, flags, extents, err);
  end_request ();
  return r;
}

/* Cache. */
static int
pause_cache (nbdkit_next *next,
             void *handle, uint32_t count, uint64_t offset, uint32_t flags,
             int *err)
{
  int r;

  begin_request ();
  r = next->cache (next, count, offset, flags, err);
  end_request ();
  return r;
}

static struct nbdkit_filter filter = {
  .name              = "pause",
  .longname          = "nbdkit pause filter",
  .unload            = pause_unload,
  .config            = pause_config,
  .config_complete   = pause_config_complete,
  .config_help       = pause_config_help,
  .after_fork        = pause_after_fork,
  .pread             = pause_pread,
  .pwrite            = pause_pwrite,
  .zero              = pause_zero,
  .trim              = pause_trim,
  .extents           = pause_extents,
  .cache             = pause_cache,
};

NBDKIT_REGISTER_FILTER (filter)
