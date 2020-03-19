/* nbdkit
 * Copyright (C) 2013-2020 Red Hat Inc.
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
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>

#ifdef HAVE_LINUX_VM_SOCKETS_H
#include <linux/vm_sockets.h>
#endif

#ifdef HAVE_LIBSELINUX
#include <selinux/selinux.h>
#endif

#include <pthread.h>

#include "internal.h"
#include "utils.h"

static void
set_selinux_label (void)
{
  if (selinux_label) {
#ifdef HAVE_LIBSELINUX
    if (setsockcreatecon_raw (selinux_label) == -1) {
      perror ("selinux-label: setsockcreatecon_raw");
      exit (EXIT_FAILURE);
    }
#else
    fprintf (stderr,
             "%s: --selinux-label option used, but "
             "this binary was compiled without SELinux support\n",
             program_name);
    exit (EXIT_FAILURE);
#endif
  }
}

static void
clear_selinux_label (void)
{
#ifdef HAVE_LIBSELINUX
  if (selinux_label) {
    if (setsockcreatecon_raw (NULL) == -1) {
      perror ("selinux-label: setsockcreatecon_raw(NULL)");
      exit (EXIT_FAILURE);
    }
  }
#endif
}

int *
bind_unix_socket (size_t *nr_socks)
{
  size_t len;
  int sock;
  struct sockaddr_un addr;
  int *ret;

  assert (unixsocket);
  assert (unixsocket[0] == '/');

  len = strlen (unixsocket);
  if (len >= UNIX_PATH_MAX) {
    fprintf (stderr, "%s: -U: path too long: length %zu > max %d bytes\n",
             program_name, len, UNIX_PATH_MAX-1);
    exit (EXIT_FAILURE);
  }

  set_selinux_label ();

#ifdef SOCK_CLOEXEC
  sock = socket (AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
#else
  /* Fortunately, this code is only run at startup, so there is no
   * risk of the fd leaking to a plugin's fork()
   */
  sock = set_cloexec (socket (AF_UNIX, SOCK_STREAM, 0));
#endif
  if (sock == -1) {
    perror ("bind_unix_socket: socket");
    exit (EXIT_FAILURE);
  }

  addr.sun_family = AF_UNIX;
  memcpy (addr.sun_path, unixsocket, len+1 /* trailing \0 */);

  if (bind (sock, (struct sockaddr *) &addr, sizeof addr) == -1) {
    perror (unixsocket);
    exit (EXIT_FAILURE);
  }

  if (listen (sock, SOMAXCONN) == -1) {
    perror ("listen");
    exit (EXIT_FAILURE);
  }

  clear_selinux_label ();

  ret = malloc (sizeof (int));
  if (!ret) {
    perror ("malloc");
    exit (EXIT_FAILURE);
  }
  ret[0] = sock;
  *nr_socks = 1;

  debug ("bound to unix socket %s", unixsocket);

  return ret;
}

int *
bind_tcpip_socket (size_t *nr_socks)
{
  struct addrinfo *ai = NULL;
  struct addrinfo hints;
  struct addrinfo *a;
  int err, opt;
  int *socks = NULL;
  bool addr_in_use = false;

  if (port == NULL)
    port = "10809";

  memset (&hints, 0, sizeof hints);
  hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
  hints.ai_socktype = SOCK_STREAM;

  err = getaddrinfo (ipaddr, port, &hints, &ai);
  if (err != 0) {
    fprintf (stderr, "%s: getaddrinfo: %s: %s: %s",
             program_name,
             ipaddr ? ipaddr : "<any>",
             port,
             gai_strerror (err));
    exit (EXIT_FAILURE);
  }

  *nr_socks = 0;

  for (a = ai; a != NULL; a = a->ai_next) {
    int sock;

    set_selinux_label ();

#ifdef SOCK_CLOEXEC
    sock = socket (a->ai_family, a->ai_socktype | SOCK_CLOEXEC, a->ai_protocol);
#else
    /* Fortunately, this code is only run at startup, so there is no
     * risk of the fd leaking to a plugin's fork()
     */
    sock = set_cloexec (socket (a->ai_family, a->ai_socktype, a->ai_protocol));
#endif
    if (sock == -1) {
      perror ("bind_tcpip_socket: socket");
      exit (EXIT_FAILURE);
    }

    opt = 1;
    if (setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt) == -1)
      perror ("setsockopt: SO_REUSEADDR");

#ifdef IPV6_V6ONLY
    if (a->ai_family == PF_INET6) {
      if (setsockopt (sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof opt) == -1)
        perror ("setsockopt: IPv6 only");
    }
#endif

    if (bind (sock, a->ai_addr, a->ai_addrlen) == -1) {
      if (errno == EADDRINUSE) {
        addr_in_use = true;
        close (sock);
        continue;
      }
      perror ("bind");
      exit (EXIT_FAILURE);
    }

    if (listen (sock, SOMAXCONN) == -1) {
      perror ("listen");
      exit (EXIT_FAILURE);
    }

    clear_selinux_label ();

    (*nr_socks)++;
    socks = realloc (socks, sizeof (int) * (*nr_socks));
    if (!socks) {
      perror ("realloc");
      exit (EXIT_FAILURE);
    }
    socks[*nr_socks - 1] = sock;
  }

  freeaddrinfo (ai);

  if (*nr_socks == 0 && addr_in_use) {
    fprintf (stderr, "%s: unable to bind to any sockets: %s\n",
             program_name, strerror (EADDRINUSE));
    exit (EXIT_FAILURE);
  }

  debug ("bound to IP address %s:%s (%zu socket(s))",
         ipaddr ? ipaddr : "<any>", port, *nr_socks);

  return socks;
}

int *
bind_vsock (size_t *nr_socks)
{
#ifdef AF_VSOCK
  uint32_t vsock_port;
  int sock;
  int *ret;
  struct sockaddr_vm addr;

  if (port == NULL)
    vsock_port = 10809;
  else {
    /* --port parameter must be numeric for vsock, unless
     * /etc/services is extended but that seems unlikely. XXX
     */
    if (nbdkit_parse_uint32_t ("port", port, &vsock_port) == -1)
      exit (EXIT_FAILURE);
  }

  /* Any platform with AF_VSOCK also supports SOCK_CLOEXEC so there is
   * no fallback path.
   */
  sock = socket (AF_VSOCK, SOCK_STREAM|SOCK_CLOEXEC, 0);
  if (sock == -1) {
    perror ("bind_vsock: socket");
    exit (EXIT_FAILURE);
  }

  memset (&addr, 0, sizeof addr);
  addr.svm_family = AF_VSOCK;
  addr.svm_cid = VMADDR_CID_ANY;
  addr.svm_port = vsock_port;

  if (bind (sock, (struct sockaddr *) &addr, sizeof addr) == -1) {
    perror (unixsocket);
    exit (EXIT_FAILURE);
  }

  if (listen (sock, SOMAXCONN) == -1) {
    perror ("listen");
    exit (EXIT_FAILURE);
  }

  ret = malloc (sizeof (int));
  if (!ret) {
    perror ("malloc");
    exit (EXIT_FAILURE);
  }
  ret[0] = sock;
  *nr_socks = 1;

  /* It's not easy to get the actual CID here.
   * IOCTL_VM_SOCKETS_GET_LOCAL_CID is documented, but requires
   * opening /dev/vsock which is not accessible to non-root users.
   * bind above doesn't update the sockaddr.  Using getsockname
   * doesn't work.
   */
  debug ("bound to vsock any:%" PRIu32, addr.svm_port);

  return ret;

#else
  /* Can't happen because main() checks if AF_VSOCK is defined and
   * prevents vsock from being set, so this function can never be
   * called.
   */
  abort ();
#endif
}

/* This counts the number of connection threads running (note: not the
 * number of worker threads, each connection thread will start many
 * worker independent threads in the current implementation).  The
 * purpose of this is so we can wait for all the connection threads to
 * exit before we return from accept_incoming_connections, so that
 * unload-time actions happen with no connections open.
 */
static pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t count_cond = PTHREAD_COND_INITIALIZER;
static unsigned count = 0;

struct thread_data {
  int sock;
  size_t instance_num;
};

static void *
start_thread (void *datav)
{
  struct thread_data *data = datav;

  debug ("accepted connection");

  pthread_mutex_lock (&count_mutex);
  count++;
  pthread_mutex_unlock (&count_mutex);

  /* Set thread-local data. */
  threadlocal_new_server_thread ();
  threadlocal_set_instance_num (data->instance_num);

  handle_single_connection (data->sock, data->sock);

  free (data);

  pthread_mutex_lock (&count_mutex);
  count--;
  pthread_cond_signal (&count_cond);
  pthread_mutex_unlock (&count_mutex);

  return NULL;
}

static void
accept_connection (int listen_sock)
{
  int err;
  pthread_attr_t attrs;
  pthread_t thread;
  struct thread_data *thread_data;
  static size_t instance_num = 1;
  const int flag = 1;

  thread_data = malloc (sizeof *thread_data);
  if (unlikely (!thread_data)) {
    perror ("malloc");
    return;
  }

  thread_data->instance_num = instance_num++;
 again:
#ifdef HAVE_ACCEPT4
  thread_data->sock = accept4 (listen_sock, NULL, NULL, SOCK_CLOEXEC);
#else
  /* If we were fully parallel, then this function could be accepting
   * connections in one thread while another thread could be in a
   * plugin trying to fork.  But plugins.c forced thread_model to
   * serialize_all_requests when it detects a lack of atomic CLOEXEC,
   * at which point, we can use a mutex to ensure we aren't accepting
   * until the plugin is not running, making non-atomicity okay.
   */
  assert (thread_model <= NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS);
  lock_request ();
  thread_data->sock = set_cloexec (accept (listen_sock, NULL, NULL));
  unlock_request ();
#endif
  if (thread_data->sock == -1) {
    if (errno == EINTR || errno == EAGAIN)
      goto again;
    perror ("accept");
    free (thread_data);
    return;
  }

  /* Disable Nagle's algorithm on this socket.  However we don't want
   * to fail if this doesn't work.
   */
  setsockopt (thread_data->sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof flag);

  /* Start a thread to handle this connection.  Note we always do this
   * even for non-threaded plugins.  There are mutexes in plugins.c
   * which ensure that non-threaded plugins are handled correctly.
   */
  pthread_attr_init (&attrs);
  pthread_attr_setdetachstate (&attrs, PTHREAD_CREATE_DETACHED);
  err = pthread_create (&thread, &attrs, start_thread, thread_data);
  pthread_attr_destroy (&attrs);
  if (unlikely (err != 0)) {
    fprintf (stderr, "%s: pthread_create: %s\n", program_name, strerror (err));
    close (thread_data->sock);
    free (thread_data);
    return;
  }

  /* If the thread starts successfully, then it is responsible for
   * closing the socket and freeing thread_data.
   */
}

/* Check the list of sockets plus quit_fd until a POLLIN event occurs
 * on any of them.
 *
 * If POLLIN occurs on quit_fd do nothing except returning early
 * (don't call accept_connection in this case).
 *
 * If POLLIN occurs on one of the sockets, call
 * accept_connection (socks[i]) on each of them.
 */
static void
check_sockets_and_quit_fd (int *socks, size_t nr_socks)
{
  size_t i;
  int r;

  CLEANUP_FREE struct pollfd *fds =
    malloc (sizeof (struct pollfd) * (nr_socks+1));
  if (fds == NULL) {
    perror ("malloc");
    exit (EXIT_FAILURE);
  }

  for (i = 0; i < nr_socks; ++i) {
    fds[i].fd = socks[i];
    fds[i].events = POLLIN;
    fds[i].revents = 0;
  }
  fds[nr_socks].fd = quit_fd;
  fds[nr_socks].events = POLLIN;
  fds[nr_socks].revents = 0;

  r = poll (fds, nr_socks + 1, -1);
  if (r == -1) {
    if (errno == EINTR || errno == EAGAIN)
      return;
    perror ("poll");
    exit (EXIT_FAILURE);
  }

  /* We don't even have to read quit_fd - just knowing that it has
   * data means the signal handler ran, so we are ready to quit the
   * loop.
   */
  if (fds[nr_socks].revents & POLLIN)
    return;

  for (i = 0; i < nr_socks; ++i) {
    if (fds[i].revents & POLLIN)
      accept_connection (socks[i]);
  }
}

void
accept_incoming_connections (int *socks, size_t nr_socks)
{
  size_t i;
  int err;

  while (!quit)
    check_sockets_and_quit_fd (socks, nr_socks);

  /* Wait for all threads to exit. */
  pthread_mutex_lock (&count_mutex);
  for (;;) {
    if (count == 0)
      break;
    err = pthread_cond_wait (&count_cond, &count_mutex);
    if (err != 0) {
      errno = err;
      perror ("pthread_cond_wait");
    }
  }
  pthread_mutex_unlock (&count_mutex);

  for (i = 0; i < nr_socks; ++i)
    close (socks[i]);
  free (socks);
}
