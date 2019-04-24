/* nbdkit
 * Copyright (C) 2013-2019 Red Hat Inc.
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
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include "internal.h"

/* Default number of parallel requests. */
#define DEFAULT_PARALLEL_REQUESTS 16

static struct connection *new_connection (int sockin, int sockout,
                                          int nworkers);
static void free_connection (struct connection *conn);

/* Don't call these raw socket functions directly.  Use conn->recv etc. */
static int raw_recv (struct connection *, void *buf, size_t len);
static int raw_send (struct connection *, const void *buf, size_t len);
static void raw_close (struct connection *);

int
connection_set_handle (struct connection *conn, size_t i, void *handle)
{
  size_t j;

  if (i < conn->nr_handles)
    conn->handles[i] = handle;
  else {
    j = conn->nr_handles;
    conn->nr_handles = i+1;
    conn->handles = realloc (conn->handles,
                             conn->nr_handles * sizeof (void *));
    if (conn->handles == NULL) {
      perror ("realloc");
      conn->nr_handles = 0;
      return -1;
    }
    for (; j < i; ++j)
      conn->handles[j] = NULL;
    conn->handles[i] = handle;
  }
  return 0;
}

void *
connection_get_handle (struct connection *conn, size_t i)
{
  if (i < conn->nr_handles)
    return conn->handles[i];
  else
    return NULL;
}

int
connection_get_status (struct connection *conn)
{
  int r;

  if (conn->nworkers &&
      pthread_mutex_lock (&conn->status_lock))
    abort ();
  r = conn->status;
  if (conn->nworkers &&
      pthread_mutex_unlock (&conn->status_lock))
    abort ();
  return r;
}

/* Update the status if the new value is lower than the existing value.
 * For convenience, return the incoming value.
 */
int
connection_set_status (struct connection *conn, int value)
{
  if (conn->nworkers &&
      pthread_mutex_lock (&conn->status_lock))
    abort ();
  if (value < conn->status)
    conn->status = value;
  if (conn->nworkers &&
      pthread_mutex_unlock (&conn->status_lock))
    abort ();
  return value;
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
  free (worker);

  while (!quit && connection_get_status (conn) > 0)
    protocol_recv_request_send_reply (conn);
  debug ("exiting worker thread %s", threadlocal_get_name ());
  free (name);
  return NULL;
}

static int
_handle_single_connection (int sockin, int sockout)
{
  const char *plugin_name;
  int ret = -1, r;
  struct connection *conn;
  int nworkers = threads ? threads : DEFAULT_PARALLEL_REQUESTS;
  pthread_t *workers = NULL;

  if (backend->thread_model (backend) < NBDKIT_THREAD_MODEL_PARALLEL ||
      nworkers == 1)
    nworkers = 0;
  conn = new_connection (sockin, sockout, nworkers);
  if (!conn)
    goto done;

  lock_request (conn);
  r = backend->open (backend, conn, readonly);
  unlock_request (conn);
  if (r == -1)
    goto done;

  /* NB: because of an asynchronous exit backend can be set to NULL at
   * just about any time.
   */
  if (backend)
    plugin_name = backend->plugin_name (backend);
  else
    plugin_name = "(unknown)";
  threadlocal_set_name (plugin_name);

  /* Prepare (for filters), called just after open. */
  lock_request (conn);
  if (backend)
    r = backend->prepare (backend, conn);
  else
    r = 0;
  unlock_request (conn);
  if (r == -1)
    goto done;

  /* Handshake. */
  if (protocol_handshake (conn) == -1)
    goto done;

  if (!nworkers) {
    /* No need for a separate thread. */
    debug ("handshake complete, processing requests serially");
    while (!quit && connection_get_status (conn) > 0)
      protocol_recv_request_send_reply (conn);
  }
  else {
    /* Create thread pool to process requests. */
    debug ("handshake complete, processing requests with %d threads",
           nworkers);
    workers = calloc (nworkers, sizeof *workers);
    if (!workers) {
      perror ("malloc");
      goto done;
    }

    for (nworkers = 0; nworkers < conn->nworkers; nworkers++) {
      struct worker_data *worker = malloc (sizeof *worker);
      int err;

      if (!worker) {
        perror ("malloc");
        connection_set_status (conn, -1);
        goto wait;
      }
      if (asprintf (&worker->name, "%s.%d", plugin_name, nworkers) < 0) {
        perror ("asprintf");
        connection_set_status (conn, -1);
        free (worker);
        goto wait;
      }
      worker->conn = conn;
      err = pthread_create (&workers[nworkers], NULL, connection_worker,
                            worker);
      if (err) {
        errno = err;
        perror ("pthread_create");
        connection_set_status (conn, -1);
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
  lock_request (conn);
  if (backend)
    r = backend->finalize (backend, conn);
  else
    r = 0;
  unlock_request (conn);
  if (r == -1)
    goto done;

  ret = connection_get_status (conn);
 done:
  free_connection (conn);
  return ret;
}

int
handle_single_connection (int sockin, int sockout)
{
  int r;

  lock_connection ();
  r = _handle_single_connection (sockin, sockout);
  unlock_connection ();

  return r;
}

static struct connection *
new_connection (int sockin, int sockout, int nworkers)
{
  struct connection *conn;

  conn = calloc (1, sizeof *conn);
  if (conn == NULL) {
    perror ("malloc");
    return NULL;
  }

  conn->status = 1;
  conn->nworkers = nworkers;
  conn->sockin = sockin;
  conn->sockout = sockout;
  pthread_mutex_init (&conn->request_lock, NULL);
  pthread_mutex_init (&conn->read_lock, NULL);
  pthread_mutex_init (&conn->write_lock, NULL);
  pthread_mutex_init (&conn->status_lock, NULL);

  conn->recv = raw_recv;
  conn->send = raw_send;
  conn->close = raw_close;

  return conn;
}

static void
free_connection (struct connection *conn)
{
  if (!conn)
    return;

  conn->close (conn);

  /* Don't call the plugin again if quit has been set because the main
   * thread will be in the process of unloading it.  The plugin.unload
   * callback should always be called.
   */
  if (!quit) {
    if (conn->nr_handles > 0 && conn->handles[0]) {
      lock_request (conn);
      backend->close (backend, conn);
      unlock_request (conn);
    }
  }

  pthread_mutex_destroy (&conn->request_lock);
  pthread_mutex_destroy (&conn->read_lock);
  pthread_mutex_destroy (&conn->write_lock);
  pthread_mutex_destroy (&conn->status_lock);

  free (conn->handles);
  free (conn);
}

/* Write buffer to conn->sockout and either succeed completely
 * (returns 0) or fail (returns -1).
 */
static int
raw_send (struct connection *conn, const void *vbuf, size_t len)
{
  int sock = conn->sockout;
  const char *buf = vbuf;
  ssize_t r;

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

/* Read buffer from conn->sockin and either succeed completely
 * (returns > 0), read an EOF (returns 0), or fail (returns -1).
 */
static int
raw_recv (struct connection *conn, void *vbuf, size_t len)
{
  int sock = conn->sockin;
  char *buf = vbuf;
  ssize_t r;
  bool first_read = true;

  while (len > 0) {
    r = read (sock, buf, len);
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
raw_close (struct connection *conn)
{
  if (conn->sockin >= 0)
    close (conn->sockin);
  if (conn->sockout >= 0 && conn->sockin != conn->sockout)
    close (conn->sockout);
}
