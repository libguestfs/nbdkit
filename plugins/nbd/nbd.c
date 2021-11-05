/* nbdkit
 * Copyright (C) 2017-2020 Red Hat Inc.
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

#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/un.h>
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/socket.h>

#include <libnbd.h>

#define NBDKIT_API_VERSION 2

#include <nbdkit-plugin.h>

#include "ascii-string.h"
#include "byte-swapping.h"
#include "cleanup.h"
#include "utils.h"
#include "vector.h"

DEFINE_VECTOR_TYPE(string_vector, const char *);

#if !defined AF_VSOCK || !LIBNBD_HAVE_NBD_CONNECT_VSOCK
#define USE_VSOCK 0
#else
#define USE_VSOCK 1
#endif

/* The per-transaction details */
struct transaction {
  int64_t cookie;
  sem_t sem;
  uint32_t early_err;
  uint32_t err;
  nbd_completion_callback cb;
};

/* The per-connection handle */
struct handle {
  /* These fields are read-only once initialized */
  struct nbd_handle *nbd;
  int fds[2]; /* Pipe for kicking the reader thread */
  bool readonly;
  pthread_t reader;
};

/* Connect to server via URI */
static const char *uri;

/* Connect to server via absolute name of Unix socket */
static char *sockname;

/* Connect to server via TCP socket */
static const char *hostname;

/* Valid with TCP or VSOCK */
static const char *port;

/* Connect to server via AF_VSOCK socket */
static const char *raw_cid;
static uint32_t cid;
static uint32_t vport;

/* Connect to a command. */
static string_vector command = empty_vector;

/* Connect to a socket file descriptor. */
static int socket_fd = -1;

/* Name of export on remote server, default '', ignored for oldstyle,
 * NULL if dynamic.
 */
static const char *export;
static bool dynamic_export;

/* Number of retries */
static unsigned retry;

/* True to share single server connection among all clients */
static bool shared;
static struct handle *shared_handle;

/* Control TLS settings */
static int tls = -1;
static char *tls_certificates;
static int tls_verify = -1;
static const char *tls_username;
static char *tls_psk;

static struct handle *nbdplug_open_handle (int readonly,
                                           const char *client_export);
static void nbdplug_close_handle (struct handle *h);

static void
nbdplug_unload (void)
{
  if (shared && shared_handle)
    nbdplug_close_handle (shared_handle);
  free (sockname);
  free (tls_certificates);
  free (tls_psk);
  free (command.ptr); /* the strings are statically allocated */
}

/* Called for each key=value passed on the command line.  See
 * nbdplug_config_help for the various keys recognized.
 */
static int
nbdplug_config (const char *key, const char *value)
{
  int r;

  if (strcmp (key, "socket") == 0) {
    /* See FILENAMES AND PATHS in nbdkit-plugin(3) */
    free (sockname);
    sockname = nbdkit_absolute_path (value);
    if (!sockname)
      return -1;
  }
  else if (strcmp (key, "hostname") == 0)
    hostname = value;
  else if (strcmp (key, "port") == 0)
    port = value;
  else if (strcmp (key, "vsock") == 0 ||
           strcmp (key, "cid") == 0)
    raw_cid = value;
  else if (strcmp (key, "uri") == 0)
    uri = value;
  else if (strcmp (key, "command") == 0 || strcmp (key, "arg") == 0) {
    if (string_vector_append (&command, value) == -1) {
      nbdkit_error ("realloc: %m");
      return -1;
    }
  }
  else if (strcmp (key, "socket-fd") == 0) {
    if (nbdkit_parse_int ("socket-fd", value, &socket_fd) == -1)
      return -1;
    if (socket_fd < 0) {
      nbdkit_error ("socket-fd must be >= 0");
      return -1;
    }
  }
  else if (strcmp (key, "export") == 0)
    export = value;
  else if (strcmp (key, "dynamic-export") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    dynamic_export = r;
  }
  else if (strcmp (key, "retry") == 0) {
    if (nbdkit_parse_unsigned ("retry", value, &retry) == -1)
      return -1;
  }
  else if (strcmp (key, "shared") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    shared = r;
  }
  else if (strcmp (key, "tls") == 0) {
    if (ascii_strcasecmp (value, "require") == 0 ||
        ascii_strcasecmp (value, "required") == 0 ||
        ascii_strcasecmp (value, "force") == 0)
      tls = LIBNBD_TLS_REQUIRE;
    else {
      r = nbdkit_parse_bool (value);
      if (r == -1)
        exit (EXIT_FAILURE);
      tls = r ? LIBNBD_TLS_ALLOW : LIBNBD_TLS_DISABLE;
    }
  }
  else if (strcmp (key, "tls-certificates") == 0) {
    free (tls_certificates);
    tls_certificates = nbdkit_absolute_path (value);
    if (!tls_certificates)
      return -1;
  }
  else if (strcmp (key, "tls-verify") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    tls_verify = r;
  }
  else if (strcmp (key, "tls-username") == 0)
    tls_username = value;
  else if (strcmp (key, "tls-psk") == 0) {
    free (tls_psk);
    tls_psk = nbdkit_absolute_path (value);
    if (!tls_psk)
      return -1;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int
nbdplug_config_complete (void)
{
  int c = !!sockname + !!hostname + !!uri +
    (command.len > 0) + (socket_fd >= 0) + !!raw_cid;

  /* Check the user passed exactly one connection parameter. */
  if (c > 1) {
    nbdkit_error ("cannot mix Unix ‘socket’, TCP ‘hostname’/‘port’, ‘vsock’, "
                  "‘command’, ‘socket-fd’ and ‘uri’ parameters");
    return -1;
  }
  if (c == 0) {
    nbdkit_error ("exactly one of ‘socket’, ‘hostname’, ‘vsock’, ‘command’, "
                  "‘socket-fd’ and ‘uri’ parameters must be specified");
    return -1;
  }

  /* Port, if present, should only be used with hostname or vsock. */
  if (port && !(hostname || raw_cid)) {
    nbdkit_error ("‘port’ parameter should only be used with ‘hostname’ or "
                  "‘vsock’");
    return -1;
  }

  if (uri) {
    struct nbd_handle *nbd = nbd_create ();

    if (!nbd) {
      nbdkit_error ("unable to query libnbd details: %s", nbd_get_error ());
      return -1;
    }
    if (!nbd_supports_uri (nbd)) {
      nbdkit_error ("libnbd was compiled without uri support");
      nbd_close (nbd);
      return -1;
    }
    nbd_close (nbd);
  }
  else if (sockname) {
    struct sockaddr_un sock;

    if (strlen (sockname) > sizeof sock.sun_path) {
      nbdkit_error ("socket file name too large");
      return -1;
    }
  }
  else if (hostname) {
    if (!port)
      port = "10809";
  }
  else if (raw_cid) {
#if !USE_VSOCK
    nbdkit_error ("libnbd was compiled without vsock support");
    return -1;
#else
    if (!port)
      port = "10809";
    if (nbdkit_parse_uint32_t ("vsock_cid", raw_cid, &cid) == -1 ||
        nbdkit_parse_uint32_t ("port", port, &vport) == -1)
      return -1;
#endif
  }
  else if (command.len > 0) {
    /* Add NULL sentinel to the command. */
    if (string_vector_append (&command, NULL) == -1) {
      nbdkit_error ("realloc: %m");
      return -1;
    }
    shared = true;
  }
  else if (socket_fd >= 0) {
    shared = true;
  }
  else {
    abort ();         /* can't happen, if checks above were correct */
  }

  /* Can't mix dynamic-export with export or shared (including
   * connection modes that imply shared).  Also, it requires
   * new-enough libnbd if uri was used.
   */
  if (dynamic_export) {
    if (export) {
      nbdkit_error ("cannot mix 'dynamic-export' with explicit export name");
      return -1;
    }
    if (shared) {
      nbdkit_error ("cannot use 'dynamic-export' with shared connection");
      return -1;
    }
#if !LIBNBD_HAVE_NBD_SET_OPT_MODE
    if (uri) {
      nbdkit_error ("libnbd too old to support 'dynamic-export' with uri "
                    "connection");
      return -1;
    }
#endif
  }
  else if (!export)
    export = "";

  /* Check the other parameters. */
  if (tls == -1)
    tls = (tls_certificates || tls_verify >= 0 || tls_username || tls_psk)
      ? LIBNBD_TLS_ALLOW : LIBNBD_TLS_DISABLE;
  if (tls != LIBNBD_TLS_DISABLE) {
    struct nbd_handle *nbd = nbd_create ();

    if (!nbd) {
      nbdkit_error ("unable to query libnbd details: %s", nbd_get_error ());
      return -1;
    }
    if (!nbd_supports_tls (nbd)) {
      nbdkit_error ("libnbd was compiled without tls support");
      nbd_close (nbd);
      return -1;
    }
    nbd_close (nbd);
  }
  return 0;
}

/* Create the shared connection.  Because this may create a background
 * thread it must be done after we fork.
 */
static int
nbdplug_after_fork (void)
{
  if (shared && (shared_handle = nbdplug_open_handle (false, NULL)) == NULL)
    return -1;
  return 0;
}

#define nbdplug_config_help \
  "[uri=]<URI>            URI of an NBD socket to connect to (if supported).\n" \
  "socket=<SOCKNAME>      The Unix socket to connect to.\n" \
  "hostname=<HOST>        The hostname for the TCP socket to connect to.\n" \
  "port=<PORT>            TCP/VSOCK port or service name to use (default 10809).\n" \
  "vsock=<CID>            The cid for the VSOCK socket to connect to.\n" \
  "command=<COMMAND>      Command to run.\n" \
  "arg=<ARG>              Parameters for command.\n" \
  "socket-fd=<FD>         Socket file descriptor to connect to.\n" \
  "export=<NAME>          Export name to connect to (default \"\").\n" \
  "dynamic-export=<BOOL>  True to enable export name pass-through.\n" \
  "retry=<N>              Retry connection up to N seconds (default 0).\n" \
  "shared=<BOOL>          True to share one server connection among all clients,\n" \
  "                       rather than a connection per client (default false).\n" \
  "tls=<MODE>             How to use TLS; one of 'off', 'on', or 'require'.\n" \
  "tls-certificates=<DIR> Directory containing files for X.509 certificates.\n" \
  "tls-verify=<BOOL>      True (default for X.509) to validate server.\n" \
  "tls-username=<NAME>    Override username presented in X.509 TLS.\n" \
  "tls-psk=<FILE>         File containing Pre-Shared Key for TLS.\n" \

static void
nbdplug_dump_plugin (void)
{
  struct nbd_handle *nbd = nbd_create ();

  if (!nbd) {
    nbdkit_error ("unable to query libnbd details: %s", nbd_get_error ());
    exit (EXIT_FAILURE);
  }
  printf ("libnbd_version=%s\n", nbd_get_version (nbd));
  printf ("libnbd_tls=%d\n", nbd_supports_tls (nbd));
  printf ("libnbd_uri=%d\n", nbd_supports_uri (nbd));
  printf ("libnbd_vsock=%d\n", USE_VSOCK);
#if LIBNBD_HAVE_NBD_OPT_LIST
  printf ("libnbd_dynamic_list=1\n");
#else
  printf ("libnbd_dynamic_list=0\n");
#endif
  nbd_close (nbd);
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Reader loop. */
void *
nbdplug_reader (void *handle)
{
  struct handle *h = handle;

  nbdkit_debug ("nbd: started reader thread");

  while (!nbd_aio_is_dead (h->nbd) && !nbd_aio_is_closed (h->nbd)) {
    int r;
    struct pollfd fds[2] = {
      [0].fd = nbd_aio_get_fd (h->nbd),
      [1].fd = h->fds[0],
      [1].events = POLLIN,
    };
    unsigned dir;

    dir = nbd_aio_get_direction (h->nbd);
    nbdkit_debug ("polling, dir=%d", dir);
    if (dir & LIBNBD_AIO_DIRECTION_READ)
      fds[0].events |= POLLIN;
    if (dir & LIBNBD_AIO_DIRECTION_WRITE)
      fds[0].events |= POLLOUT;
    if (poll (fds, 2, -1) == -1) {
      nbdkit_error ("poll: %m");
      break;
    }

    dir = nbd_aio_get_direction (h->nbd);

    r = 0;
    if ((dir & LIBNBD_AIO_DIRECTION_READ) && (fds[0].revents & POLLIN))
      r = nbd_aio_notify_read (h->nbd);
    else if ((dir & LIBNBD_AIO_DIRECTION_WRITE) && (fds[0].revents & POLLOUT))
      r = nbd_aio_notify_write (h->nbd);
    if (r == -1) {
      nbdkit_error ("%s", nbd_get_error ());
      break;
    }

    /* Check if we were kicked because a command was started */
    if (fds[1].revents & POLLIN) {
      char buf[10]; /* Larger than 1 to allow reduction of any backlog */

      if (read (h->fds[0], buf, sizeof buf) == -1 && errno != EAGAIN) {
        nbdkit_error ("failed to read pipe: %m");
        break;
      }
    }
  }

  nbdkit_debug ("state machine changed to %s", nbd_connection_state (h->nbd));
  nbdkit_debug ("exiting reader thread");
  return NULL;
}

/* Callback used at end of a transaction. */
static int
nbdplug_notify (void *opaque, int *error)
{
  struct transaction *trans = opaque;

  /* There's a possible race here where trans->cookie has not yet been
   * updated by nbdplug_register, but it's only an informational
   * message.
   */
  nbdkit_debug ("cookie %" PRId64 " completed state machine, status %d",
                trans->cookie, *error);
  trans->err = *error;
  if (sem_post (&trans->sem)) {
    nbdkit_error ("failed to post semaphore: %m");
    abort ();
  }
  return 1;
}

/* Prepare for a transaction. */
static void
nbdplug_prepare (struct transaction *trans)
{
  memset (trans, 0, sizeof *trans);
  if (sem_init (&trans->sem, 0, 0))
    assert (false);
  trans->cb.callback = nbdplug_notify;
  trans->cb.user_data = trans;
}

/* Register a cookie and kick the I/O thread. */
static void
nbdplug_register (struct handle *h, struct transaction *trans, int64_t cookie)
{
  char c = 0;

  if (cookie == -1) {
    nbdkit_error ("command failed: %s", nbd_get_error ());
    trans->early_err = nbd_get_errno ();
    return;
  }

  nbdkit_debug ("cookie %" PRId64 " started by state machine", cookie);
  trans->cookie = cookie;

  if (write (h->fds[1], &c, 1) == -1 && errno != EAGAIN)
    nbdkit_debug ("failed to kick reader thread: %m");
}

/* Perform the reply half of a transaction. */
static int
nbdplug_reply (struct handle *h, struct transaction *trans)
{
  int err;

  if (trans->early_err)
    err = trans->early_err;
  else {
    while ((err = sem_wait (&trans->sem)) == -1 && errno == EINTR)
      /* try again */;
    if (err) {
      nbdkit_debug ("failed to wait on semaphore: %m");
      err = EIO;
    }
    else
      err = trans->err;
  }
  if (sem_destroy (&trans->sem))
    abort ();
  errno = err;
  return err ? -1 : 0;
}

/* Move an nbd handle from created to negotiating/ready.  Error reporting
 * is left to the caller.
 */
static int
nbdplug_connect (struct nbd_handle *nbd)
{
  if (tls_certificates &&
      nbd_set_tls_certificates (nbd, tls_certificates) == -1)
    return -1;
  if (tls_verify >= 0 && nbd_set_tls_verify_peer (nbd, tls_verify) == -1)
    return -1;
  if (tls_username && nbd_set_tls_username (nbd, tls_username) == -1)
    return -1;
  if (tls_psk && nbd_set_tls_psk_file (nbd, tls_psk) == -1)
    return -1;
  if (uri)
    return nbd_connect_uri (nbd, uri);
  else if (sockname)
    return nbd_connect_unix (nbd, sockname);
  else if (hostname)
    return nbd_connect_tcp (nbd, hostname, port);
  else if (raw_cid)
#if !USE_VSOCK
    abort ();
#else
    return nbd_connect_vsock (nbd, cid, vport);
#endif
  else if (command.len > 0)
    return nbd_connect_systemd_socket_activation (nbd, (char **) command.ptr);
  else if (socket_fd >= 0)
    return nbd_connect_socket (nbd, socket_fd);
  else
    abort ();
}

/* Create the shared or per-connection handle. */
static struct handle *
nbdplug_open_handle (int readonly, const char *client_export)
{
  struct handle *h;
  unsigned long retries = retry;

  h = calloc (1, sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }
#ifdef HAVE_PIPE2
  if (pipe2 (h->fds, O_NONBLOCK)) {
    nbdkit_error ("pipe2: %m");
    free (h);
    return NULL;
  }
#else
  /* This plugin doesn't fork, so we don't care about CLOEXEC. Our use
   * of pipe2 is merely for convenience.
   */
  if (pipe (h->fds)) {
    nbdkit_error ("pipe: %m");
    free (h);
    return NULL;
  }
  if (set_nonblock (h->fds[0]) == -1) {
    close (h->fds[1]);
    free (h);
    return NULL;
  }
  if (set_nonblock (h->fds[1]) == -1) {
    close (h->fds[0]);
    free (h);
    return NULL;
  }
#endif

  if (dynamic_export)
    assert (client_export);
  else
    client_export = export;

 retry:
  h->nbd = nbd_create ();
  if (!h->nbd)
    goto errnbd;
  if (nbd_set_export_name (h->nbd, client_export) == -1)
    goto errnbd;
  if (nbd_add_meta_context (h->nbd, LIBNBD_CONTEXT_BASE_ALLOCATION) == -1)
    goto errnbd;
#if LIBNBD_HAVE_NBD_SET_FULL_INFO
  if (nbd_set_full_info (h->nbd, 1) == -1)
    goto errnbd;
#endif
  if (dynamic_export && uri) {
#if LIBNBD_HAVE_NBD_SET_OPT_MODE
    if (nbd_set_opt_mode (h->nbd, 1) == -1)
      goto errnbd;
#else
    abort (); /* Prevented by .config_complete */
#endif
  }
  if (nbd_set_tls (h->nbd, tls) == -1)
    goto errnbd;
  if (nbdplug_connect (h->nbd) == -1) {
    if (retries--) {
      nbdkit_debug ("connect failed; will try again: %s", nbd_get_error ());
      nbd_close (h->nbd);
      sleep (1);
      goto retry;
    }
    goto errnbd;
  }

#if LIBNBD_HAVE_NBD_SET_OPT_MODE
  /* Oldstyle servers can't change export name, but that's okay. */
  if (uri && dynamic_export && nbd_aio_is_negotiating (h->nbd)) {
    if (nbd_set_export_name (h->nbd, client_export) == -1)
      goto errnbd;
    if (nbd_opt_go (h->nbd) == -1)
      goto errnbd;
  }
#endif

  if (readonly)
    h->readonly = true;

  /* Spawn a dedicated reader thread */
  if ((errno = pthread_create (&h->reader, NULL, nbdplug_reader, h))) {
    nbdkit_error ("failed to initialize reader thread: %m");
    goto err;
  }

  return h;

 errnbd:
  nbdkit_error ("failure while creating nbd handle: %s", nbd_get_error ());
 err:
  close (h->fds[0]);
  close (h->fds[1]);
  if (h->nbd)
    nbd_close (h->nbd);
  free (h);
  return NULL;
}

#if LIBNBD_HAVE_NBD_OPT_LIST
static int
collect_one (void *opaque, const char *name, const char *desc)
{
  struct nbdkit_exports *exports = opaque;

  if (nbdkit_add_export (exports, name, desc) == -1)
    nbdkit_debug ("Unable to share export %s: %s", name, nbd_get_error ());
  return 0;
}
#endif /* LIBNBD_HAVE_NBD_OPT_LIST */

/* Export list. */
static int
nbdplug_list_exports (int readonly, int is_tls, struct nbdkit_exports *exports)
{
#if LIBNBD_HAVE_NBD_OPT_LIST
  if (dynamic_export) {
    struct nbd_handle *nbd = nbd_create ();
    int r = -1;

    if (!nbd)
      goto out;
    if (nbd_set_opt_mode (nbd, 1) == -1)
      goto out;
    if (nbdplug_connect (nbd) == -1)
      goto out;
    if (nbd_opt_list (nbd, (nbd_list_callback) { .callback = collect_one,
                                                 .user_data = exports }) == -1)
      goto out;
    r = 0;
  out:
    if (r == -1)
      nbdkit_error ("Unable to get list: %s", nbd_get_error ());
    if (nbd) {
      if (nbd_aio_is_negotiating (nbd))
        nbd_opt_abort (nbd);
      else if (nbd_aio_is_ready (nbd))
        nbd_shutdown (nbd, 0);
      nbd_close (nbd);
    }
    return r;
  }
#endif
  return nbdkit_use_default_export (exports);
}

/* Canonical name of default export. */
static const char *
nbdplug_default_export (int readonly, int is_tls)
{
  const char *ret = "";
  CLEANUP_FREE char *name = NULL;

  if (!dynamic_export)
    return export;
#if LIBNBD_HAVE_NBD_SET_FULL_INFO
  /* Best effort determination of server's canonical name.  If it
   * fails, we're fine using the default name on our end (NBD_OPT_GO
   * might still work on "" later on).
   */
  struct nbd_handle *nbd = nbd_create ();

  if (!nbd)
    return "";
  if (nbd_set_full_info (nbd, 1) == -1)
    goto out;
  if (nbd_set_opt_mode (nbd, 1) == -1)
    goto out;
  if (nbdplug_connect (nbd) == -1)
    goto out;
  if (nbd_set_export_name (nbd, "") == -1)
    goto out;
  if (nbd_opt_info (nbd) == -1)
    goto out;
  name = nbd_get_canonical_export_name (nbd);
  if (name)
    ret = nbdkit_strdup_intern (name);

 out:
  if (nbd_aio_is_negotiating (nbd))
    nbd_opt_abort (nbd);
  else if (nbd_aio_is_ready (nbd))
    nbd_shutdown (nbd, 0);
  nbd_close (nbd);
#endif
  return ret;
}

/* Create the per-connection handle. */
static void *
nbdplug_open (int readonly)
{
  if (shared)
    return shared_handle;
  return nbdplug_open_handle (readonly, nbdkit_export_name ());
}

/* Free up the shared or per-connection handle. */
static void
nbdplug_close_handle (struct handle *h)
{
  if (nbd_aio_disconnect (h->nbd, 0) == -1)
    nbdkit_debug ("failed to clean up handle: %s", nbd_get_error ());
  if ((errno = pthread_join (h->reader, NULL)))
    nbdkit_debug ("failed to join reader thread: %m");
  close (h->fds[0]);
  close (h->fds[1]);
  nbd_close (h->nbd);
  free (h);
}

/* Free up the per-connection handle. */
static void
nbdplug_close (void *handle)
{
  struct handle *h = handle;

  if (!shared)
    nbdplug_close_handle (h);
}

/* Description. */
static const char *
nbdplug_export_description (void *handle)
{
#if LIBNBD_HAVE_NBD_GET_EXPORT_DESCRIPTION
  struct handle *h = handle;
  CLEANUP_FREE char *desc = nbd_get_export_description (h->nbd);
  if (desc)
    return nbdkit_strdup_intern (desc);
#endif
  return NULL;
}

/* Get the file size. */
static int64_t
nbdplug_get_size (void *handle)
{
  struct handle *h = handle;
  int64_t size = nbd_get_size (h->nbd);

  if (size == -1) {
    nbdkit_error ("failure to get size: %s", nbd_get_error ());
    return -1;
  }
  return size;
}

static int
nbdplug_can_write (void *handle)
{
  struct handle *h = handle;
  int i = nbd_is_read_only (h->nbd);

  if (i == -1) {
    nbdkit_error ("failure to check readonly flag: %s", nbd_get_error ());
    return -1;
  }
  return !(i || h->readonly);
}

static int
nbdplug_can_flush (void *handle)
{
  struct handle *h = handle;
  int i = nbd_can_flush (h->nbd);

  if (i == -1) {
    nbdkit_error ("failure to check flush flag: %s", nbd_get_error ());
    return -1;
  }
  return i;
}

static int
nbdplug_is_rotational (void *handle)
{
  struct handle *h = handle;
  int i = nbd_is_rotational (h->nbd);

  if (i == -1) {
    nbdkit_error ("failure to check rotational flag: %s", nbd_get_error ());
    return -1;
  }
  return i;
}

static int
nbdplug_can_trim (void *handle)
{
  struct handle *h = handle;
  int i = nbd_can_trim (h->nbd);

  if (i == -1) {
    nbdkit_error ("failure to check trim flag: %s", nbd_get_error ());
    return -1;
  }
  return i;
}

static int
nbdplug_can_zero (void *handle)
{
  struct handle *h = handle;
  int i = nbd_can_zero (h->nbd);

  if (i == -1) {
    nbdkit_error ("failure to check zero flag: %s", nbd_get_error ());
    return -1;
  }
  return i;
}

static int
nbdplug_can_fast_zero (void *handle)
{
#if LIBNBD_HAVE_NBD_CAN_FAST_ZERO
  struct handle *h = handle;
  int i = nbd_can_fast_zero (h->nbd);

  if (i == -1) {
    nbdkit_error ("failure to check fast zero flag: %s", nbd_get_error ());
    return -1;
  }
  return i;
#else
  /* libnbd 0.9.8 lacks fast zero support */
  return 0;
#endif
}

static int
nbdplug_can_fua (void *handle)
{
  struct handle *h = handle;
  int i = nbd_can_fua (h->nbd);

  if (i == -1) {
    nbdkit_error ("failure to check fua flag: %s", nbd_get_error ());
    return -1;
  }
  return i ? NBDKIT_FUA_NATIVE : NBDKIT_FUA_NONE;
}

static int
nbdplug_can_multi_conn (void *handle)
{
  struct handle *h = handle;
  int i = nbd_can_multi_conn (h->nbd);

  if (i == -1) {
    nbdkit_error ("failure to check multi-conn flag: %s", nbd_get_error ());
    return -1;
  }
  return i;
}

static int
nbdplug_can_cache (void *handle)
{
  struct handle *h = handle;
  int i = nbd_can_cache (h->nbd);

  if (i == -1) {
    nbdkit_error ("failure to check cache flag: %s", nbd_get_error ());
    return -1;
  }
  return i ? NBDKIT_CACHE_NATIVE : NBDKIT_CACHE_NONE;
}

static int
nbdplug_can_extents (void *handle)
{
  struct handle *h = handle;
  int i = nbd_can_meta_context (h->nbd, LIBNBD_CONTEXT_BASE_ALLOCATION);

  if (i == -1) {
    nbdkit_error ("failure to check extents ability: %s", nbd_get_error ());
    return -1;
  }
  return i;
}

/* Read data from the file. */
static int
nbdplug_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
               uint32_t flags)
{
  struct handle *h = handle;
  struct transaction s;

  assert (!flags);
  nbdplug_prepare (&s);
  nbdplug_register (h, &s, nbd_aio_pread (h->nbd, buf, count, offset,
                                          s.cb, 0));
  return nbdplug_reply (h, &s);
}

/* Write data to the file. */
static int
nbdplug_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
                uint32_t flags)
{
  struct handle *h = handle;
  struct transaction s;
  uint32_t f = flags & NBDKIT_FLAG_FUA ? LIBNBD_CMD_FLAG_FUA : 0;

  assert (!(flags & ~NBDKIT_FLAG_FUA));
  nbdplug_prepare (&s);
  nbdplug_register (h, &s, nbd_aio_pwrite (h->nbd, buf, count, offset,
                                           s.cb, f));
  return nbdplug_reply (h, &s);
}

/* Write zeroes to the file. */
static int
nbdplug_zero (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  struct handle *h = handle;
  struct transaction s;
  uint32_t f = 0;

  assert (!(flags & ~(NBDKIT_FLAG_FUA | NBDKIT_FLAG_MAY_TRIM |
                      NBDKIT_FLAG_FAST_ZERO)));

  if (!(flags & NBDKIT_FLAG_MAY_TRIM))
    f |= LIBNBD_CMD_FLAG_NO_HOLE;
  if (flags & NBDKIT_FLAG_FUA)
    f |= LIBNBD_CMD_FLAG_FUA;
#if LIBNBD_HAVE_NBD_CAN_FAST_ZERO
  if (flags & NBDKIT_FLAG_FAST_ZERO)
    f |= LIBNBD_CMD_FLAG_FAST_ZERO;
#else
  assert (!(flags & NBDKIT_FLAG_FAST_ZERO));
#endif
  nbdplug_prepare (&s);
  nbdplug_register (h, &s, nbd_aio_zero (h->nbd, count, offset, s.cb, f));
  return nbdplug_reply (h, &s);
}

/* Trim a portion of the file. */
static int
nbdplug_trim (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  struct handle *h = handle;
  struct transaction s;
  uint32_t f = flags & NBDKIT_FLAG_FUA ? LIBNBD_CMD_FLAG_FUA : 0;

  assert (!(flags & ~NBDKIT_FLAG_FUA));
  nbdplug_prepare (&s);
  nbdplug_register (h, &s, nbd_aio_trim (h->nbd, count, offset, s.cb, f));
  return nbdplug_reply (h, &s);
}

/* Flush the file to disk. */
static int
nbdplug_flush (void *handle, uint32_t flags)
{
  struct handle *h = handle;
  struct transaction s;

  assert (!flags);
  nbdplug_prepare (&s);
  nbdplug_register (h, &s, nbd_aio_flush (h->nbd, s.cb, 0));
  return nbdplug_reply (h, &s);
}

static int
nbdplug_extent (void *opaque, const char *metacontext, uint64_t offset,
                uint32_t *entries, size_t nr_entries, int *error)
{
  struct nbdkit_extents *extents = opaque;

  assert (strcmp (metacontext, LIBNBD_CONTEXT_BASE_ALLOCATION) == 0);
  assert (nr_entries % 2 == 0);
  while (nr_entries) {
    /* We rely on the fact that NBDKIT_EXTENT_* match NBD_STATE_* */
    if (nbdkit_add_extent (extents, offset, entries[0], entries[1]) == -1) {
      *error = errno;
      return -1;
    }
    offset += entries[0];
    entries += 2;
    nr_entries -= 2;
  }
  return 0;
}

/* Read extents of the file. */
static int
nbdplug_extents (void *handle, uint32_t count, uint64_t offset,
                 uint32_t flags, struct nbdkit_extents *extents)
{
  struct handle *h = handle;
  struct transaction s;
  uint32_t f = flags & NBDKIT_FLAG_REQ_ONE ? LIBNBD_CMD_FLAG_REQ_ONE : 0;
  nbd_extent_callback extcb = { nbdplug_extent, extents };

  assert (!(flags & ~NBDKIT_FLAG_REQ_ONE));
  nbdplug_prepare (&s);
  nbdplug_register (h, &s, nbd_aio_block_status (h->nbd, count, offset,
                                                 extcb, s.cb, f));
  return nbdplug_reply (h, &s);
}

/* Cache a portion of the file. */
static int
nbdplug_cache (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  struct handle *h = handle;
  struct transaction s;

  assert (!flags);
  nbdplug_prepare (&s);
  nbdplug_register (h, &s, nbd_aio_cache (h->nbd, count, offset, s.cb, 0));
  return nbdplug_reply (h, &s);
}

static struct nbdkit_plugin plugin = {
  .name               = "nbd",
  .longname           = "nbdkit nbd plugin",
  .version            = PACKAGE_VERSION,
  .unload             = nbdplug_unload,
  .config             = nbdplug_config,
  .config_complete    = nbdplug_config_complete,
  .config_help        = nbdplug_config_help,
  .magic_config_key   = "uri",
  .after_fork         = nbdplug_after_fork,
  .dump_plugin        = nbdplug_dump_plugin,
  .list_exports       = nbdplug_list_exports,
  .default_export     = nbdplug_default_export,
  .open               = nbdplug_open,
  .close              = nbdplug_close,
  .export_description = nbdplug_export_description,
  .get_size           = nbdplug_get_size,
  .can_write          = nbdplug_can_write,
  .can_flush          = nbdplug_can_flush,
  .is_rotational      = nbdplug_is_rotational,
  .can_trim           = nbdplug_can_trim,
  .can_zero           = nbdplug_can_zero,
  .can_fast_zero      = nbdplug_can_fast_zero,
  .can_fua            = nbdplug_can_fua,
  .can_multi_conn     = nbdplug_can_multi_conn,
  .can_extents        = nbdplug_can_extents,
  .can_cache          = nbdplug_can_cache,
  .pread              = nbdplug_pread,
  .pwrite             = nbdplug_pwrite,
  .zero               = nbdplug_zero,
  .flush              = nbdplug_flush,
  .trim               = nbdplug_trim,
  .extents            = nbdplug_extents,
  .cache              = nbdplug_cache,
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN (plugin)
