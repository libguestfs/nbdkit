/* nbdkit
 * Copyright (C) 2013-2022 Red Hat Inc.
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
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#include "internal.h"
#include "utils.h"

/* Default number of parallel requests. */
#define DEFAULT_PARALLEL_REQUESTS 16

static struct connection *new_connection (int sockin, int sockout,
                                          int nworkers);
static void free_connection (struct connection *conn);

/* Don't call these raw socket functions directly.  Use conn->recv etc. */
static int raw_recv ( void *buf, size_t len);
static int raw_send_socket (const void *buf, size_t len, int flags);
#ifndef WIN32
static int raw_send_other (const void *buf, size_t len, int flags);
#endif
static void raw_close (int how);

conn_status
connection_get_status (void)
{
  GET_CONN;
  conn_status r;

  if (conn->nworkers &&
      pthread_mutex_lock (&conn->status_lock))
    abort ();
  r = conn->status;
  if (conn->nworkers &&
      pthread_mutex_unlock (&conn->status_lock))
    abort ();
  return r;
}

/* Update the status if the new value is lower than the existing value. */
void
connection_set_status (conn_status value)
{
  GET_CONN;

  if (conn->nworkers &&
      pthread_mutex_lock (&conn->status_lock))
    abort ();
  if (value < conn->status) {
    if (conn->nworkers && conn->status > STATUS_CLIENT_DONE) {
      char c = 0;

      assert (conn->status_pipe[1] >= 0);
      if (write (conn->status_pipe[1], &c, 1) != 1 && errno != EAGAIN)
        debug ("failed to notify pipe-to-self: %m");
    }
    if (conn->status >= STATUS_CLIENT_DONE && value < STATUS_CLIENT_DONE)
      conn->close (SHUT_WR);
    conn->status = value;
  }
  if (conn->nworkers &&
      pthread_mutex_unlock (&conn->status_lock))
    abort ();
}

struct worker_data {
  struct connection *conn;
  char *name;
};

static void *
connection_worker (void *data)
{
  struct worker_data *worker = data;
  struct connection *conn = worker->conn;
  char *name = worker->name;

  debug ("starting worker thread %s", name);
  threadlocal_new_server_thread ();
  threadlocal_set_name (name);
  threadlocal_set_conn (conn);
  free (worker);

  while (!quit && connection_get_status () > STATUS_CLIENT_DONE)
    protocol_recv_request_send_reply ();
  debug ("exiting worker thread %s", threadlocal_get_name ());
  free (name);
  return NULL;
}

void
handle_single_connection (int sockin, int sockout)
{
  const char *plugin_name;
  int r;
  struct connection *conn;
  int nworkers = threads ? threads : DEFAULT_PARALLEL_REQUESTS;
  pthread_t *workers = NULL;

  lock_connection ();

  /* Because of asynchronous exit it is plausible that a new
   * connection is started at the same time as the backend is being
   * shut down.  top may therefore be NULL, and if this happens return
   * immediately.
   */
  if (!top) {
    unlock_connection ();
    return;
  }

  if (thread_model < NBDKIT_THREAD_MODEL_PARALLEL || nworkers == 1)
    nworkers = 0;
  conn = new_connection (sockin, sockout, nworkers);
  if (!conn)
    goto done;

  plugin_name = top->plugin_name (top);
  threadlocal_set_name (plugin_name);

  if (top->preconnect (top, read_only) == -1)
    goto done;

  /* NBD handshake.
   *
   * Note that this calls the backend .open callback when it is safe
   * to do so (eg. after TLS authentication).
   */
  if (protocol_handshake () == -1)
    goto done;
  conn->handshake_complete = true;

  if (!nworkers) {
    /* No need for a separate thread. */
    debug ("handshake complete, processing requests serially");
    while (!quit && connection_get_status () > STATUS_CLIENT_DONE)
      protocol_recv_request_send_reply ();
  }
  else {
    /* Create thread pool to process requests. */
    debug ("handshake complete, processing requests with %d threads",
           nworkers);
    workers = calloc (nworkers, sizeof *workers);
    if (unlikely (!workers)) {
      perror ("malloc");
      goto done;
    }

    for (nworkers = 0; nworkers < conn->nworkers; nworkers++) {
      struct worker_data *worker = malloc (sizeof *worker);
      int err;

      if (unlikely (!worker)) {
        perror ("malloc");
        connection_set_status (STATUS_DEAD);
        goto wait;
      }
      if (unlikely (asprintf (&worker->name, "%s.%d", plugin_name, nworkers)
                    < 0)) {
        perror ("asprintf");
        connection_set_status (STATUS_DEAD);
        free (worker);
        goto wait;
      }
      worker->conn = conn;
      err = pthread_create (&workers[nworkers], NULL, connection_worker,
                            worker);
      if (unlikely (err)) {
        errno = err;
        perror ("pthread_create");
        connection_set_status (STATUS_DEAD);
        free (worker);
        goto wait;
      }
    }

  wait:
    while (nworkers)
      pthread_join (workers[--nworkers], NULL);
    free (workers);
  }

  /* Finalize (for filters), called just before close. */
  lock_request ();
  r = backend_finalize (conn->top_context);
  unlock_request ();
  if (r == -1)
    goto done;

 done:
  free_connection (conn);
  unlock_connection ();
}

static struct connection *
new_connection (int sockin, int sockout, int nworkers)
{
  struct connection *conn;
#ifndef WIN32
  int opt;
  socklen_t optlen = sizeof opt;
#endif

  conn = calloc (1, sizeof *conn);
  if (conn == NULL) {
    perror ("malloc");
    return NULL;
  }
  conn->status_pipe[0] = conn->status_pipe[1] = -1;

  pthread_mutex_init (&conn->request_lock, NULL);
  pthread_mutex_init (&conn->read_lock, NULL);
  pthread_mutex_init (&conn->write_lock, NULL);
  pthread_mutex_init (&conn->status_lock, NULL);

  conn->default_exportname = calloc (top->i + 1,
                                     sizeof *conn->default_exportname);
  if (conn->default_exportname == NULL) {
    perror ("malloc");
    goto error1;
  }

  conn->status = STATUS_ACTIVE;
  conn->nworkers = nworkers;
  if (nworkers) {
#ifdef HAVE_PIPE2
    if (pipe2 (conn->status_pipe, O_NONBLOCK | O_CLOEXEC)) {
      perror ("pipe2");
      goto error2;
    }
#else
#ifdef HAVE_PIPE
    /* If we were fully parallel, then this function could be
     * accepting connections in one thread while another thread could
     * be in a plugin trying to fork.  But plugins.c forced
     * thread_model to serialize_all_requests when it detects a lack
     * of atomic CLOEXEC, at which point, we can use a mutex to ensure
     * we aren't accepting until the plugin is not running, making
     * non-atomicity okay.
     */
    assert (thread_model <= NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS);
    lock_request ();
    if (pipe (conn->status_pipe)) {
      perror ("pipe");
      unlock_request ();
      goto error2;
    }
    if (set_nonblock (set_cloexec (conn->status_pipe[0])) == -1) {
      perror ("fcntl");
      close (conn->status_pipe[1]);
      unlock_request ();
      goto error2;
    }
    if (set_nonblock (set_cloexec (conn->status_pipe[1])) == -1) {
      perror ("fcntl");
      close (conn->status_pipe[0]);
      unlock_request ();
      goto error2;
    }
    unlock_request ();
#else /* !HAVE_PIPE2 && !HAVE_PIPE */
    /* Windows has neither pipe2 nor pipe. XXX */
#endif
#endif
  }

  conn->sockin = sockin;
  conn->sockout = sockout;
  conn->recv = raw_recv;
#ifndef WIN32
  if (getsockopt (sockout, SOL_SOCKET, SO_TYPE, &opt, &optlen) == 0)
    conn->send = raw_send_socket;
  else
    conn->send = raw_send_other;
#else
  conn->send = raw_send_socket;
#endif
  conn->close = raw_close;

  threadlocal_set_conn (conn);

  return conn;

#if defined(HAVE_PIPE2) || defined(HAVE_PIPE)
 error2:
#endif
  if (conn->status_pipe[0] >= 0)
    close (conn->status_pipe[0]);
  if (conn->status_pipe[1] >= 0)
    close (conn->status_pipe[1]);
  free (conn->default_exportname);

 error1:
  pthread_mutex_destroy (&conn->request_lock);
  pthread_mutex_destroy (&conn->read_lock);
  pthread_mutex_destroy (&conn->write_lock);
  pthread_mutex_destroy (&conn->status_lock);
  free (conn);
  return NULL;
}

static void
free_connection (struct connection *conn)
{
  struct backend *b;

  if (!conn)
    return;

  conn->close (SHUT_RDWR);

  /* Don't call the plugin again if quit has been set because the main
   * thread will be in the process of unloading it.  The plugin.unload
   * callback should always be called.
   */
  if (!quit) {
    lock_request ();
    if (conn->top_context) {
      backend_close (conn->top_context);
      conn->top_context = NULL;
    }
    unlock_request ();
  }

  if (conn->status_pipe[0] >= 0) {
    close (conn->status_pipe[0]);
    close (conn->status_pipe[1]);
  }

  pthread_mutex_destroy (&conn->request_lock);
  pthread_mutex_destroy (&conn->read_lock);
  pthread_mutex_destroy (&conn->write_lock);
  pthread_mutex_destroy (&conn->status_lock);

  free (conn->exportname_from_set_meta_context);
  free_interns ();

  for_each_backend (b)
    free (conn->default_exportname[b->i]);
  free (conn->default_exportname);

  free (conn);
  threadlocal_set_conn (NULL);
}

/* Write buffer to conn->sockout with send() and either succeed completely
 * (returns 0) or fail (returns -1). flags may include SEND_MORE as a hint
 * that this send will be followed by related data.
 */
static int
raw_send_socket (const void *vbuf, size_t len, int flags)
{
  GET_CONN;
  int sock = conn->sockout;
  const char *buf = vbuf;
  ssize_t r;
  int f = 0;

  assert (sock >= 0);
#ifdef MSG_MORE
  if (flags & SEND_MORE)
    f |= MSG_MORE;
#endif
  while (len > 0) {
    r = send (sock, buf, len, f);
    if (r == -1) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      return -1;
    }
    buf += r;
    len -= r;
  }

  return 0;
}

#ifndef WIN32
/* Write buffer to conn->sockout with write() and either succeed completely
 * (returns 0) or fail (returns -1). flags is ignored.
 */
static int
raw_send_other (const void *vbuf, size_t len, int flags)
{
  GET_CONN;
  int sock = conn->sockout;
  const char *buf = vbuf;
  ssize_t r;

  assert (sock >= 0);
  while (len > 0) {
    r = write (sock, buf, len);
    if (r == -1) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      return -1;
    }
    buf += r;
    len -= r;
  }

  return 0;
}
#endif /* !WIN32 */

/* Read buffer from conn->sockin and either succeed completely
 * (returns > 0), read an EOF (returns 0), or fail (returns -1).
 */
static int
raw_recv (void *vbuf, size_t len)
{
  GET_CONN;
  int sock = conn->sockin;
  char *buf = vbuf;
  ssize_t r;
  bool first_read = true;

  while (len > 0) {
    /* On Unix we want to use read(2) here because that allows us to
     * read from non-sockets (think: nbdkit -s).  In particular this
     * makes fuzzing possible.  However this is not possible on
     * Windows where we must use recv.
     */
#ifndef WIN32
    r = read (sock, buf, len);
#else
    r = recv (sock, buf, len, 0);
#endif
    if (r == -1) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      return -1;
    }
    if (r == 0) {
      if (first_read)
        return 0;
      /* Partial record read.  This is an error. */
      errno = EBADMSG;
      return -1;
    }
    first_read = false;
    buf += r;
    len -= r;
  }

  return 1;
}

/* There's no place in the NBD protocol to send back errors from
 * close, so this function ignores errors.
 */
static void
raw_close (int how)
{
  GET_CONN;

  if (conn->sockout >= 0 && how == SHUT_WR) {
    if (conn->sockin == conn->sockout)
      shutdown (conn->sockout, how);
    else
      closesocket (conn->sockout);
    conn->sockout = -1;
  }
  else {
    if (conn->sockin >= 0)
      closesocket (conn->sockin);
    if (conn->sockout >= 0 && conn->sockin != conn->sockout)
      closesocket (conn->sockout);
  }
}
