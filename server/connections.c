/* nbdkit
 * Copyright (C) 2013-2018 Red Hat Inc.
 * All rights reserved.
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
#include <errno.h>
#include <sys/types.h>
#include <stddef.h>
#include <assert.h>

#include <pthread.h>

#include "internal.h"
#include "byte-swapping.h"
#include "protocol.h"

/* Maximum read or write request that we will handle. */
#define MAX_REQUEST_SIZE (64 * 1024 * 1024)

/* Maximum number of client options we allow before giving up. */
#define MAX_NR_OPTIONS 32

/* Maximum length of any option data (bytes). */
#define MAX_OPTION_LENGTH 4096

/* Default number of parallel requests. */
#define DEFAULT_PARALLEL_REQUESTS 16

/* Connection structure. */
struct connection {
  pthread_mutex_t request_lock;
  pthread_mutex_t read_lock;
  pthread_mutex_t write_lock;
  pthread_mutex_t status_lock;
  int status; /* 1 for more I/O with client, 0 for shutdown, -1 on error */
  void *crypto_session;
  int nworkers;

  void **handles;
  size_t nr_handles;

  uint32_t cflags;
  uint64_t exportsize;
  uint16_t eflags;
  bool readonly;
  bool can_flush;
  bool is_rotational;
  bool can_trim;
  bool can_zero;
  bool can_fua;
  bool can_multi_conn;
  bool using_tls;
  bool structured_replies;

  int sockin, sockout;
  connection_recv_function recv;
  connection_send_function send;
  connection_close_function close;
};

static struct connection *new_connection (int sockin, int sockout,
                                          int nworkers);
static void free_connection (struct connection *conn);
static int negotiate_handshake (struct connection *conn);
static int recv_request_send_reply (struct connection *conn);

/* Don't call these raw socket functions directly.  Use conn->recv etc. */
static int raw_recv (struct connection *, void *buf, size_t len);
static int raw_send (struct connection *, const void *buf, size_t len);
static void raw_close (struct connection *);

/* Accessors for public fields in the connection structure.
 * Everything else is private to this file.
 */
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

pthread_mutex_t *
connection_get_request_lock (struct connection *conn)
{
  return &conn->request_lock;
}

void
connection_set_crypto_session (struct connection *conn, void *session)
{
  conn->crypto_session = session;
}

void *
connection_get_crypto_session (struct connection *conn)
{
  return conn->crypto_session;
}

/* The code in crypto.c uses these three functions to replace the
 * recv, send and close callbacks when a connection is upgraded to
 * TLS.
 */
void
connection_set_recv (struct connection *conn, connection_recv_function recv)
{
  conn->recv = recv;
}

void
connection_set_send (struct connection *conn, connection_send_function send)
{
  conn->send = send;
}

void
connection_set_close (struct connection *conn, connection_close_function close)
{
  conn->close = close;
}

static int
get_status (struct connection *conn)
{
  int r;

  if (conn->nworkers)
    pthread_mutex_lock (&conn->status_lock);
  r = conn->status;
  if (conn->nworkers)
    pthread_mutex_unlock (&conn->status_lock);
  return r;
}

/* Update the status if the new value is lower than the existing value.
 * For convenience, return the incoming value. */
static int
set_status (struct connection *conn, int value)
{
  if (conn->nworkers)
    pthread_mutex_lock (&conn->status_lock);
  if (value < conn->status)
    conn->status = value;
  if (conn->nworkers)
    pthread_mutex_unlock (&conn->status_lock);
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

  while (!quit && get_status (conn) > 0)
    recv_request_send_reply (conn);
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
  if (negotiate_handshake (conn) == -1)
    goto done;

  if (!nworkers) {
    /* No need for a separate thread. */
    debug ("handshake complete, processing requests serially");
    while (!quit && get_status (conn) > 0)
      recv_request_send_reply (conn);
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
        set_status (conn, -1);
        goto wait;
      }
      if (asprintf (&worker->name, "%s.%d", plugin_name, nworkers) < 0) {
        perror ("asprintf");
        set_status (conn, -1);
        free (worker);
        goto wait;
      }
      worker->conn = conn;
      err = pthread_create (&workers[nworkers], NULL, connection_worker,
                            worker);
      if (err) {
        errno = err;
        perror ("pthread_create");
        set_status (conn, -1);
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

  ret = get_status (conn);
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

static int
compute_eflags (struct connection *conn, uint16_t *flags)
{
  uint16_t eflags = NBD_FLAG_HAS_FLAGS;
  int fl;

  fl = backend->can_write (backend, conn);
  if (fl == -1)
    return -1;
  if (readonly || !fl) {
    eflags |= NBD_FLAG_READ_ONLY;
    conn->readonly = true;
  }
  if (!conn->readonly) {
    fl = backend->can_zero (backend, conn);
    if (fl == -1)
      return -1;
    if (fl) {
      eflags |= NBD_FLAG_SEND_WRITE_ZEROES;
      conn->can_zero = true;
    }

    fl = backend->can_trim (backend, conn);
    if (fl == -1)
      return -1;
    if (fl) {
      eflags |= NBD_FLAG_SEND_TRIM;
      conn->can_trim = true;
    }

    fl = backend->can_fua (backend, conn);
    if (fl == -1)
      return -1;
    if (fl) {
      eflags |= NBD_FLAG_SEND_FUA;
      conn->can_fua = true;
    }
  }

  fl = backend->can_flush (backend, conn);
  if (fl == -1)
    return -1;
  if (fl) {
    eflags |= NBD_FLAG_SEND_FLUSH;
    conn->can_flush = true;
  }

  fl = backend->is_rotational (backend, conn);
  if (fl == -1)
    return -1;
  if (fl) {
    eflags |= NBD_FLAG_ROTATIONAL;
    conn->is_rotational = true;
  }

  fl = backend->can_multi_conn (backend, conn);
  if (fl == -1)
    return -1;
  if (fl) {
    eflags |= NBD_FLAG_CAN_MULTI_CONN;
    conn->can_multi_conn = true;
  }

  *flags = eflags;
  return 0;
}

static int
_negotiate_handshake_oldstyle (struct connection *conn)
{
  struct old_handshake handshake;
  int64_t r;
  uint64_t exportsize;
  uint16_t gflags, eflags;

  /* In --tls=require / FORCEDTLS mode, old style handshakes are
   * rejected because they cannot support TLS.
   */
  if (tls == 2) {
    nbdkit_error ("non-TLS client tried to connect in --tls=require mode");
    return -1;
  }

  r = backend->get_size (backend, conn);
  if (r == -1)
    return -1;
  if (r < 0) {
    nbdkit_error (".get_size function returned invalid value "
                  "(%" PRIi64 ")", r);
    return -1;
  }
  exportsize = (uint64_t) r;
  conn->exportsize = exportsize;

  gflags = 0;
  if (compute_eflags (conn, &eflags) < 0)
    return -1;

  debug ("oldstyle negotiation: flags: global 0x%x export 0x%x",
         gflags, eflags);

  memset (&handshake, 0, sizeof handshake);
  memcpy (handshake.nbdmagic, "NBDMAGIC", 8);
  handshake.version = htobe64 (OLD_VERSION);
  handshake.exportsize = htobe64 (exportsize);
  handshake.gflags = htobe16 (gflags);
  handshake.eflags = htobe16 (eflags);

  if (conn->send (conn, &handshake, sizeof handshake) == -1) {
    nbdkit_error ("write: %m");
    return -1;
  }

  return 0;
}

/* Receive newstyle options. */

static int
send_newstyle_option_reply (struct connection *conn,
                            uint32_t option, uint32_t reply)
{
  struct fixed_new_option_reply fixed_new_option_reply;

  fixed_new_option_reply.magic = htobe64 (NBD_REP_MAGIC);
  fixed_new_option_reply.option = htobe32 (option);
  fixed_new_option_reply.reply = htobe32 (reply);
  fixed_new_option_reply.replylen = htobe32 (0);

  if (conn->send (conn,
                  &fixed_new_option_reply,
                  sizeof fixed_new_option_reply) == -1) {
    nbdkit_error ("write: %m");
    return -1;
  }

  return 0;
}

static int
send_newstyle_option_reply_exportname (struct connection *conn,
                                       uint32_t option, uint32_t reply,
                                       const char *exportname)
{
  struct fixed_new_option_reply fixed_new_option_reply;
  size_t name_len = strlen (exportname);
  uint32_t len;

  fixed_new_option_reply.magic = htobe64 (NBD_REP_MAGIC);
  fixed_new_option_reply.option = htobe32 (option);
  fixed_new_option_reply.reply = htobe32 (reply);
  fixed_new_option_reply.replylen = htobe32 (name_len + sizeof (len));

  if (conn->send (conn,
                  &fixed_new_option_reply,
                  sizeof fixed_new_option_reply) == -1) {
    nbdkit_error ("write: %m");
    return -1;
  }

  len = htobe32 (name_len);
  if (conn->send (conn, &len, sizeof len) == -1) {
    nbdkit_error ("write: %m");
    return -1;
  }
  if (conn->send (conn, exportname, name_len) == -1) {
    nbdkit_error ("write: %m");
    return -1;
  }

  return 0;
}

static int
send_newstyle_option_reply_info_export (struct connection *conn,
                                        uint32_t option, uint32_t reply,
                                        uint16_t info)
{
  struct fixed_new_option_reply fixed_new_option_reply;
  struct fixed_new_option_reply_info_export export;

  fixed_new_option_reply.magic = htobe64 (NBD_REP_MAGIC);
  fixed_new_option_reply.option = htobe32 (option);
  fixed_new_option_reply.reply = htobe32 (reply);
  fixed_new_option_reply.replylen = htobe32 (sizeof export);
  export.info = htobe16 (info);
  export.exportsize = htobe64 (conn->exportsize);
  export.eflags = htobe16 (conn->eflags);

  if (conn->send (conn,
                  &fixed_new_option_reply,
                  sizeof fixed_new_option_reply) == -1 ||
      conn->send (conn, &export, sizeof export) == -1) {
    nbdkit_error ("write: %m");
    return -1;
  }

  return 0;
}

/* Sub-function during _negotiate_handshake_newstyle, to uniformly handle
 * a client hanging up on a message boundary.
 */
static int __attribute__ ((format (printf, 4, 5)))
conn_recv_full (struct connection *conn, void *buf, size_t len,
                const char *fmt, ...)
{
  int r = conn->recv (conn, buf, len);
  va_list args;

  if (r == -1) {
    va_start (args, fmt);
    nbdkit_verror (fmt, args);
    va_end (args);
    return -1;
  }
  if (r == 0) {
    /* During negotiation, client EOF on message boundary is less
     * severe than failure in the middle of the buffer. */
    debug ("client closed input socket, closing connection");
    return -1;
  }
  return r;
}

/* Sub-function of _negotiate_handshake_newstyle_options below.  It
 * must be called on all non-error paths out of the options for-loop
 * in that function.
 */
static int
finish_newstyle_options (struct connection *conn)
{
  int64_t r;

  r = backend->get_size (backend, conn);
  if (r == -1)
    return -1;
  if (r < 0) {
    nbdkit_error (".get_size function returned invalid value "
                  "(%" PRIi64 ")", r);
    return -1;
  }
  conn->exportsize = (uint64_t) r;

  if (compute_eflags (conn, &conn->eflags) < 0)
    return -1;

  debug ("newstyle negotiation: flags: export 0x%x", conn->eflags);
  return 0;
}

static int
_negotiate_handshake_newstyle_options (struct connection *conn)
{
  struct new_option new_option;
  size_t nr_options;
  uint64_t version;
  uint32_t option;
  uint32_t optlen;
  char data[MAX_OPTION_LENGTH+1];
  struct new_handshake_finish handshake_finish;
  const char *optname;

  for (nr_options = 0; nr_options < MAX_NR_OPTIONS; ++nr_options) {
    if (conn_recv_full (conn, &new_option, sizeof new_option,
                        "reading option: conn->recv: %m") == -1)
      return -1;

    version = be64toh (new_option.version);
    if (version != NEW_VERSION) {
      nbdkit_error ("unknown option version %" PRIx64
                    ", expecting %" PRIx64,
                    version, NEW_VERSION);
      return -1;
    }

    /* There is a maximum option length we will accept, regardless
     * of the option type.
     */
    optlen = be32toh (new_option.optlen);
    if (optlen > MAX_OPTION_LENGTH) {
      nbdkit_error ("client option data too long (%" PRIu32 ")", optlen);
      return -1;
    }

    option = be32toh (new_option.option);

    /* In --tls=require / FORCEDTLS mode the only options allowed
     * before TLS negotiation are NBD_OPT_ABORT and NBD_OPT_STARTTLS.
     */
    if (tls == 2 && !conn->using_tls &&
        !(option == NBD_OPT_ABORT || option == NBD_OPT_STARTTLS)) {
      if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_TLS_REQD))
        return -1;
      continue;
    }

    switch (option) {
    case NBD_OPT_EXPORT_NAME:
      if (conn_recv_full (conn, data, optlen,
                          "read: %s: %m", name_of_nbd_opt (option)) == -1)
        return -1;
      /* Apart from printing it, ignore the export name. */
      data[optlen] = '\0';
      debug ("newstyle negotiation: %s: "
             "client requested export '%s' (ignored)",
             name_of_nbd_opt (option), data);

      /* We have to finish the handshake by sending handshake_finish. */
      if (finish_newstyle_options (conn) == -1)
        return -1;

      memset (&handshake_finish, 0, sizeof handshake_finish);
      handshake_finish.exportsize = htobe64 (conn->exportsize);
      handshake_finish.eflags = htobe16 (conn->eflags);

      if (conn->send (conn,
                      &handshake_finish,
                      (conn->cflags & NBD_FLAG_NO_ZEROES)
                      ? offsetof (struct new_handshake_finish, zeroes)
                      : sizeof handshake_finish) == -1) {
        nbdkit_error ("write: %m");
        return -1;
      }
      break;

    case NBD_OPT_ABORT:
      if (send_newstyle_option_reply (conn, option, NBD_REP_ACK) == -1)
        return -1;
      debug ("client sent %s to abort the connection",
             name_of_nbd_opt (option));
      return -1;

    case NBD_OPT_LIST:
      if (optlen != 0) {
        if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_INVALID)
            == -1)
          return -1;
        if (conn_recv_full (conn, data, optlen,
                            "read: %s: %m", name_of_nbd_opt (option)) == -1)
          return -1;
        continue;
      }

      /* Send back the exportname. */
      debug ("newstyle negotiation: %s: advertising export '%s'",
             name_of_nbd_opt (option), exportname);
      if (send_newstyle_option_reply_exportname (conn, option, NBD_REP_SERVER,
                                                 exportname) == -1)
        return -1;

      if (send_newstyle_option_reply (conn, option, NBD_REP_ACK) == -1)
        return -1;
      break;

    case NBD_OPT_STARTTLS:
      if (optlen != 0) {
        if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_INVALID)
            == -1)
          return -1;
        if (conn_recv_full (conn, data, optlen,
                            "read: %s: %m", name_of_nbd_opt (option)) == -1)
          return -1;
        continue;
      }

      if (tls == 0) {           /* --tls=off (NOTLS mode). */
#ifdef HAVE_GNUTLS
#define NO_TLS_REPLY NBD_REP_ERR_POLICY
#else
#define NO_TLS_REPLY NBD_REP_ERR_UNSUP
#endif
        if (send_newstyle_option_reply (conn, option, NO_TLS_REPLY) == -1)
          return -1;
      }
      else /* --tls=on or --tls=require */ {
        /* We can't upgrade to TLS twice on the same connection. */
        if (conn->using_tls) {
          if (send_newstyle_option_reply (conn, option,
                                          NBD_REP_ERR_INVALID) == -1)
            return -1;
          continue;
        }

        /* We have to send the (unencrypted) reply before starting
         * the handshake.
         */
        if (send_newstyle_option_reply (conn, option, NBD_REP_ACK) == -1)
          return -1;

        /* Upgrade the connection to TLS.  Also performs access control. */
        if (crypto_negotiate_tls (conn, conn->sockin, conn->sockout) == -1)
          return -1;
        conn->using_tls = true;
        debug ("using TLS on this connection");
      }
      break;

    case NBD_OPT_INFO:
    case NBD_OPT_GO:
      optname = name_of_nbd_opt (option);
      if (conn_recv_full (conn, data, optlen,
                          "read: %s: %m", optname) == -1)
        return -1;

      if (optlen < 6) { /* 32 bit export length + 16 bit nr info */
        debug ("newstyle negotiation: %s option length < 6", optname);

        if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_INVALID)
            == -1)
          return -1;
        continue;
      }

      {
        uint32_t exportnamelen;
        uint16_t nrinfos;
        uint16_t info;
        size_t i;
        CLEANUP_FREE char *requested_exportname = NULL;

        /* Validate the name length and number of INFO requests. */
        memcpy (&exportnamelen, &data[0], 4);
        exportnamelen = be32toh (exportnamelen);
        if (exportnamelen > optlen-6 /* NB optlen >= 6, see above */) {
          debug ("newstyle negotiation: %s: export name too long", optname);
          if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_INVALID)
              == -1)
            return -1;
          continue;
        }
        memcpy (&nrinfos, &data[exportnamelen+4], 2);
        nrinfos = be16toh (nrinfos);
        if (optlen != 4 + exportnamelen + 2 + 2*nrinfos) {
          debug ("newstyle negotiation: %s: "
                 "number of information requests incorrect", optname);
          if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_INVALID)
              == -1)
            return -1;
          continue;
        }

        /* As with NBD_OPT_EXPORT_NAME we print the export name and then
         * ignore it.
         */
        requested_exportname = malloc (exportnamelen+1);
        if (requested_exportname == NULL) {
          nbdkit_error ("malloc: %m");
          return -1;
        }
        memcpy (requested_exportname, &data[4], exportnamelen);
        requested_exportname[exportnamelen] = '\0';
        debug ("newstyle negotiation: %s: "
               "client requested export '%s' (ignored)",
               optname, requested_exportname);

        /* The spec is confusing, but it is required that we send back
         * NBD_INFO_EXPORT, even if the client did not request it!
         * qemu client in particular does not request this, but will
         * fail if we don't send it.
         */
        if (finish_newstyle_options (conn) == -1)
          return -1;

        if (send_newstyle_option_reply_info_export (conn, option,
                                                    NBD_REP_INFO,
                                                    NBD_INFO_EXPORT) == -1)
          return -1;

        /* For now we ignore all other info requests (but we must
         * ignore NBD_INFO_EXPORT if it was requested, because we
         * replied already above).  Therefore this loop doesn't do
         * much at the moment.
         */
        for (i = 0; i < nrinfos; ++i) {
          memcpy (&info, &data[4 + exportnamelen + 2 + i*2], 2);
          info = be16toh (info);
          switch (info) {
          case NBD_INFO_EXPORT: /* ignore - reply sent above */ break;
          default:
            debug ("newstyle negotiation: %s: "
                   "ignoring NBD_INFO_* request %u (%s)",
                   optname, (unsigned) info, name_of_nbd_info (info));
            break;
          }
        }
      }

      /* Unlike NBD_OPT_EXPORT_NAME, NBD_OPT_GO sends back an ACK
       * or ERROR packet.
       */
      if (send_newstyle_option_reply (conn, option, NBD_REP_ACK) == -1)
        return -1;

      break;

    case NBD_OPT_STRUCTURED_REPLY:
      if (optlen != 0) {
        if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_INVALID)
            == -1)
          return -1;
        if (conn_recv_full (conn, data, optlen,
                            "read: %s: %m", name_of_nbd_opt (option)) == -1)
          return -1;
        continue;
      }

      debug ("newstyle negotiation: %s: client requested structured replies",
             name_of_nbd_opt (option));

      if (send_newstyle_option_reply (conn, option, NBD_REP_ACK) == -1)
        return -1;

      conn->structured_replies = true;
      break;

    default:
      /* Unknown option. */
      if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_UNSUP) == -1)
        return -1;
      if (conn_recv_full (conn, data, optlen,
                          "reading unknown option data: conn->recv: %m") == -1)
        return -1;
    }

    /* Note, since it's not very clear from the protocol doc, that the
     * client must send NBD_OPT_EXPORT_NAME or NBD_OPT_GO last, and
     * that ends option negotiation.
     */
    if (option == NBD_OPT_EXPORT_NAME || option == NBD_OPT_GO)
      break;
  }

  if (nr_options >= MAX_NR_OPTIONS) {
    nbdkit_error ("client exceeded maximum number of options (%d)",
                  MAX_NR_OPTIONS);
    return -1;
  }

  /* In --tls=require / FORCEDTLS mode, we must have upgraded to TLS
   * by the time we finish option negotiation.  If not, give up.
   */
  if (tls == 2 && !conn->using_tls) {
    nbdkit_error ("non-TLS client tried to connect in --tls=require mode");
    return -1;
  }

  return 0;
}

static int
_negotiate_handshake_newstyle (struct connection *conn)
{
  struct new_handshake handshake;
  uint16_t gflags;

  gflags = NBD_FLAG_FIXED_NEWSTYLE | NBD_FLAG_NO_ZEROES;

  debug ("newstyle negotiation: flags: global 0x%x", gflags);

  memcpy (handshake.nbdmagic, "NBDMAGIC", 8);
  handshake.version = htobe64 (NEW_VERSION);
  handshake.gflags = htobe16 (gflags);

  if (conn->send (conn, &handshake, sizeof handshake) == -1) {
    nbdkit_error ("write: %m");
    return -1;
  }

  /* Client now sends us its 32 bit flags word ... */
  if (conn_recv_full (conn, &conn->cflags, sizeof conn->cflags,
                      "reading initial client flags: conn->recv: %m") == -1)
    return -1;
  conn->cflags = be32toh (conn->cflags);
  /* ... which we check for accuracy. */
  debug ("newstyle negotiation: client flags: 0x%x", conn->cflags);
  if (conn->cflags & ~gflags) {
    nbdkit_error ("client requested unknown flags 0x%x", conn->cflags);
    return -1;
  }

  /* Receive newstyle options. */
  if (_negotiate_handshake_newstyle_options (conn) == -1)
    return -1;

  return 0;
}

static int
negotiate_handshake (struct connection *conn)
{
  int r;

  lock_request (conn);
  if (!newstyle)
    r = _negotiate_handshake_oldstyle (conn);
  else
    r = _negotiate_handshake_newstyle (conn);
  unlock_request (conn);

  return r;
}

static bool
valid_range (struct connection *conn, uint64_t offset, uint32_t count)
{
  uint64_t exportsize = conn->exportsize;

  return count > 0 && offset <= exportsize && offset + count <= exportsize;
}

static bool
validate_request (struct connection *conn,
                  uint16_t cmd, uint16_t flags, uint64_t offset, uint32_t count,
                  uint32_t *error)
{
  /* Readonly connection? */
  if (conn->readonly &&
      (cmd == NBD_CMD_WRITE || cmd == NBD_CMD_TRIM ||
       cmd == NBD_CMD_WRITE_ZEROES)) {
    nbdkit_error ("invalid request: %s: write request on readonly connection",
                  name_of_nbd_cmd (cmd));
    *error = EROFS;
    return false;
  }

  /* Validate cmd, offset, count. */
  switch (cmd) {
  case NBD_CMD_READ:
  case NBD_CMD_WRITE:
  case NBD_CMD_TRIM:
  case NBD_CMD_WRITE_ZEROES:
    if (!valid_range (conn, offset, count)) {
      /* XXX Allow writes to extend the disk? */
      nbdkit_error ("invalid request: %s: offset and count are out of range: "
                    "offset=%" PRIu64 " count=%" PRIu32,
                    name_of_nbd_cmd (cmd), offset, count);
      *error = (cmd == NBD_CMD_WRITE ||
                cmd == NBD_CMD_WRITE_ZEROES) ? ENOSPC : EINVAL;
      return false;
    }
    break;

  case NBD_CMD_FLUSH:
    if (offset != 0 || count != 0) {
      nbdkit_error ("invalid request: %s: expecting offset and count = 0",
                    name_of_nbd_cmd (cmd));
      *error = EINVAL;
      return false;
    }
    break;

  default:
    nbdkit_error ("invalid request: unknown command (%" PRIu32 ") ignored",
                  cmd);
    *error = EINVAL;
    return false;
  }

  /* Validate flags */
  if (flags & ~(NBD_CMD_FLAG_FUA | NBD_CMD_FLAG_NO_HOLE)) {
    nbdkit_error ("invalid request: unknown flag (0x%x)", flags);
    *error = EINVAL;
    return false;
  }
  if ((flags & NBD_CMD_FLAG_NO_HOLE) &&
      cmd != NBD_CMD_WRITE_ZEROES) {
    nbdkit_error ("invalid request: NO_HOLE flag needs WRITE_ZEROES request");
    *error = EINVAL;
    return false;
  }
  if (!conn->can_fua && (flags & NBD_CMD_FLAG_FUA)) {
    nbdkit_error ("invalid request: FUA flag not supported");
    *error = EINVAL;
    return false;
  }

  /* Refuse over-large read and write requests. */
  if ((cmd == NBD_CMD_WRITE || cmd == NBD_CMD_READ) &&
      count > MAX_REQUEST_SIZE) {
    nbdkit_error ("invalid request: %s: data request is too large (%" PRIu32
                  " > %d)",
                  name_of_nbd_cmd (cmd), count, MAX_REQUEST_SIZE);
    *error = ENOMEM;
    return false;
  }

  /* Flush allowed? */
  if (!conn->can_flush && cmd == NBD_CMD_FLUSH) {
    nbdkit_error ("invalid request: %s: flush operation not supported",
                  name_of_nbd_cmd (cmd));
    *error = EINVAL;
    return false;
  }

  /* Trim allowed? */
  if (!conn->can_trim && cmd == NBD_CMD_TRIM) {
    nbdkit_error ("invalid request: %s: trim operation not supported",
                  name_of_nbd_cmd (cmd));
    *error = EINVAL;
    return false;
  }

  /* Zero allowed? */
  if (!conn->can_zero && cmd == NBD_CMD_WRITE_ZEROES) {
    nbdkit_error ("invalid request: %s: write zeroes operation not supported",
                  name_of_nbd_cmd (cmd));
    *error = EINVAL;
    return false;
  }

  return true;                     /* Command validates. */
}

/* This is called with the request lock held to actually execute the
 * request (by calling the plugin).  Note that the request fields have
 * been validated already in 'validate_request' so we don't have to
 * check them again.  'buf' is either the data to be written or the
 * data to be returned, and points to a buffer of size 'count' bytes.
 *
 * In all cases, the return value is the system errno value that will
 * later be converted to the nbd error to send back to the client (0
 * for success).
 */
static uint32_t
handle_request (struct connection *conn,
                uint16_t cmd, uint16_t flags, uint64_t offset, uint32_t count,
                void *buf)
{
  uint32_t f = 0;
  bool fua = conn->can_fua && (flags & NBD_CMD_FLAG_FUA);
  int err = 0;

  /* Clear the error, so that we know if the plugin calls
   * nbdkit_set_error() or relied on errno.  */
  threadlocal_set_error (0);

  switch (cmd) {
  case NBD_CMD_READ:
    if (backend->pread (backend, conn, buf, count, offset, 0, &err) == -1)
      return err;
    break;

  case NBD_CMD_WRITE:
    if (fua)
      f |= NBDKIT_FLAG_FUA;
    if (backend->pwrite (backend, conn, buf, count, offset, f, &err) == -1)
      return err;
    break;

  case NBD_CMD_FLUSH:
    if (backend->flush (backend, conn, 0, &err) == -1)
      return err;
    break;

  case NBD_CMD_TRIM:
    if (fua)
      f |= NBDKIT_FLAG_FUA;
    if (backend->trim (backend, conn, count, offset, f, &err) == -1)
      return err;
    break;

  case NBD_CMD_WRITE_ZEROES:
    if (!(flags & NBD_CMD_FLAG_NO_HOLE))
      f |= NBDKIT_FLAG_MAY_TRIM;
    if (fua)
      f |= NBDKIT_FLAG_FUA;
    if (backend->zero (backend, conn, count, offset, f, &err) == -1)
      return err;
    break;

  default:
    abort ();
  }

  return 0;
}

static int
skip_over_write_buffer (int sock, size_t count)
{
  char buf[BUFSIZ];
  ssize_t r;

  if (count > MAX_REQUEST_SIZE * 2) {
    nbdkit_error ("write request too large to skip");
    return -1;
  }

  while (count > 0) {
    r = read (sock, buf, count > BUFSIZ ? BUFSIZ : count);
    if (r == -1) {
      nbdkit_error ("skipping write buffer: %m");
      return -1;
    }
    if (r == 0)  {
      nbdkit_error ("unexpected early EOF");
      errno = EBADMSG;
      return -1;
    }
    count -= r;
  }
  return 0;
}

/* Convert a system errno to an NBD_E* error code. */
static int
nbd_errno (int error)
{
  switch (error) {
  case 0:
    return NBD_SUCCESS;
  case EROFS:
  case EPERM:
    return NBD_EPERM;
  case EIO:
    return NBD_EIO;
  case ENOMEM:
    return NBD_ENOMEM;
#ifdef EDQUOT
  case EDQUOT:
#endif
  case EFBIG:
  case ENOSPC:
    return NBD_ENOSPC;
#ifdef ESHUTDOWN
  case ESHUTDOWN:
    return NBD_ESHUTDOWN;
#endif
  case EINVAL:
  default:
    return NBD_EINVAL;
  }
}

static int
send_simple_reply (struct connection *conn,
                   uint64_t handle, uint16_t cmd,
                   const char *buf, uint32_t count,
                   uint32_t error)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&conn->write_lock);
  struct simple_reply reply;
  int r;

  reply.magic = htobe32 (NBD_SIMPLE_REPLY_MAGIC);
  reply.handle = handle;
  reply.error = htobe32 (nbd_errno (error));

  r = conn->send (conn, &reply, sizeof reply);
  if (r == -1) {
    nbdkit_error ("write reply: %s: %m", name_of_nbd_cmd (cmd));
    return set_status (conn, -1);
  }

  /* Send the read data buffer. */
  if (cmd == NBD_CMD_READ && !error) {
    r = conn->send (conn, buf, count);
    if (r == -1) {
      nbdkit_error ("write data: %s: %m", name_of_nbd_cmd (cmd));
      return set_status (conn, -1);
    }
  }

  return 1;                     /* command processed ok */
}

static int
send_structured_reply_read (struct connection *conn,
                            uint64_t handle, uint16_t cmd,
                            const char *buf, uint32_t count, uint64_t offset)
{
  /* Once we are really using structured replies and sending data back
   * in chunks, we'll be able to grab the write lock for each chunk,
   * allowing other threads to interleave replies.  As we're not doing
   * that yet we acquire the lock for the whole function.
   */
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&conn->write_lock);
  struct structured_reply reply;
  struct structured_reply_offset_data offset_data;
  int r;

  assert (cmd == NBD_CMD_READ);

  reply.magic = htobe32 (NBD_STRUCTURED_REPLY_MAGIC);
  reply.handle = handle;
  reply.flags = htobe16 (NBD_REPLY_FLAG_DONE);
  reply.type = htobe16 (NBD_REPLY_TYPE_OFFSET_DATA);
  reply.length = htobe32 (count + sizeof offset_data);

  r = conn->send (conn, &reply, sizeof reply);
  if (r == -1) {
    nbdkit_error ("write reply: %s: %m", name_of_nbd_cmd (cmd));
    return set_status (conn, -1);
  }

  /* Send the offset + read data buffer. */
  offset_data.offset = htobe64 (offset);
  r = conn->send (conn, &offset_data, sizeof offset_data);
  if (r == -1) {
    nbdkit_error ("write data: %s: %m", name_of_nbd_cmd (cmd));
    return set_status (conn, -1);
  }

  r = conn->send (conn, buf, count);
  if (r == -1) {
    nbdkit_error ("write data: %s: %m", name_of_nbd_cmd (cmd));
    return set_status (conn, -1);
  }

  return 1;                     /* command processed ok */
}

static int
send_structured_reply_error (struct connection *conn,
                             uint64_t handle, uint16_t cmd, uint32_t error)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&conn->write_lock);
  struct structured_reply reply;
  struct structured_reply_error error_data;
  int r;

  reply.magic = htobe32 (NBD_STRUCTURED_REPLY_MAGIC);
  reply.handle = handle;
  reply.flags = htobe16 (NBD_REPLY_FLAG_DONE);
  reply.type = htobe16 (NBD_REPLY_TYPE_ERROR);
  reply.length = htobe32 (0 /* no human readable error */ + sizeof error_data);

  r = conn->send (conn, &reply, sizeof reply);
  if (r == -1) {
    nbdkit_error ("write error reply: %m");
    return set_status (conn, -1);
  }

  /* Send the error. */
  error_data.error = htobe32 (error);
  error_data.len = htobe16 (0);
  r = conn->send (conn, &error_data, sizeof error_data);
  if (r == -1) {
    nbdkit_error ("write data: %s: %m", name_of_nbd_cmd (cmd));
    return set_status (conn, -1);
  }
  /* No human readable error message at the moment. */

  return 1;                     /* command processed ok */
}

static int
recv_request_send_reply (struct connection *conn)
{
  int r;
  struct request request;
  uint16_t cmd, flags;
  uint32_t magic, count, error = 0;
  uint64_t offset;
  CLEANUP_FREE char *buf = NULL;

  /* Read the request packet. */
  {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&conn->read_lock);
    r = get_status (conn);
    if (r <= 0)
      return r;
    r = conn->recv (conn, &request, sizeof request);
    if (r == -1) {
      nbdkit_error ("read request: %m");
      return set_status (conn, -1);
    }
    if (r == 0) {
      debug ("client closed input socket, closing connection");
      return set_status (conn, 0);                   /* disconnect */
    }

    magic = be32toh (request.magic);
    if (magic != NBD_REQUEST_MAGIC) {
      nbdkit_error ("invalid request: 'magic' field is incorrect (0x%x)",
                    magic);
      return set_status (conn, -1);
    }

    flags = be16toh (request.flags);
    cmd = be16toh (request.type);

    offset = be64toh (request.offset);
    count = be32toh (request.count);

    if (cmd == NBD_CMD_DISC) {
      debug ("client sent %s, closing connection", name_of_nbd_cmd (cmd));
      return set_status (conn, 0);                   /* disconnect */
    }

    /* Validate the request. */
    if (!validate_request (conn, cmd, flags, offset, count, &error)) {
      if (cmd == NBD_CMD_WRITE &&
          skip_over_write_buffer (conn->sockin, count) < 0)
        return set_status (conn, -1);
      goto send_reply;
    }

    /* Allocate the data buffer used for either read or write requests. */
    if (cmd == NBD_CMD_READ || cmd == NBD_CMD_WRITE) {
      buf = malloc (count);
      if (buf == NULL) {
        perror ("malloc");
        error = ENOMEM;
        if (cmd == NBD_CMD_WRITE &&
            skip_over_write_buffer (conn->sockin, count) < 0)
          return set_status (conn, -1);
        goto send_reply;
      }
    }

    /* Receive the write data buffer. */
    if (cmd == NBD_CMD_WRITE) {
      r = conn->recv (conn, buf, count);
      if (r == 0) {
        errno = EBADMSG;
        r = -1;
      }
      if (r == -1) {
        nbdkit_error ("read data: %s: %m", name_of_nbd_cmd (cmd));
        return set_status (conn, -1);
      }
    }
  }

  /* Perform the request.  Only this part happens inside the request lock. */
  if (quit || !get_status (conn)) {
    error = ESHUTDOWN;
  }
  else {
    lock_request (conn);
    error = handle_request (conn, cmd, flags, offset, count, buf);
    assert ((int) error >= 0);
    unlock_request (conn);
  }

  /* Send the reply packet. */
 send_reply:
  if (get_status (conn) < 0)
    return -1;

  if (error != 0) {
    /* Since we're about to send only the limited NBD_E* errno to the
     * client, don't lose the information about what really happened
     * on the server side.  Make sure there is a way for the operator
     * to retrieve the real error.
     */
    debug ("sending error reply: %s", strerror (error));
  }

  /* Currently we prefer to send simple replies for everything except
   * where we have to (ie. NBD_CMD_READ when structured_replies have
   * been negotiated).  However this prevents us from sending
   * human-readable error messages to the client, so we should
   * reconsider this in future.
   */
  if (conn->structured_replies && cmd == NBD_CMD_READ) {
    if (!error)
      return send_structured_reply_read (conn, request.handle, cmd,
                                         buf, count, offset);
    else
      return send_structured_reply_error (conn, request.handle, cmd, error);
  }
  else
    return send_simple_reply (conn, request.handle, cmd, buf, count, error);
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
