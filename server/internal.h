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

#ifndef NBDKIT_INTERNAL_H
#define NBDKIT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <pthread.h>

#define NBDKIT_API_VERSION 2
#include "nbdkit-plugin.h"
#include "nbdkit-filter.h"
#include "cleanup.h"

#ifdef __APPLE__
#define UNIX_PATH_MAX 104
#else
#define UNIX_PATH_MAX 108
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

#if HAVE_VALGRIND
# include <valgrind.h>
/* http://valgrind.org/docs/manual/faq.html#faq.unhelpful */
# define DO_DLCLOSE !RUNNING_ON_VALGRIND
#elif defined(__SANITIZE_ADDRESS__)
# define DO_DLCLOSE 0
#else
# define DO_DLCLOSE 1
#endif

#define container_of(ptr, type, member) ({                       \
      const typeof (((type *) 0)->member) *__mptr = (ptr);       \
      (type *) ((char *) __mptr - offsetof(type, member));       \
    })

/* main.c */
struct debug_flag {
  struct debug_flag *next;
  char *name;                   /* plugin or filter name */
  char *flag;                   /* flag name */
  int value;                    /* value of flag */
  bool used;                    /* if flag was successfully set */
};

enum log_to {
  LOG_TO_DEFAULT,        /* --log not specified: log to stderr, unless
                            we forked into the background in which
                            case log to syslog */
  LOG_TO_STDERR,         /* --log=stderr forced on the command line */
  LOG_TO_SYSLOG,         /* --log=syslog forced on the command line */
};

extern struct debug_flag *debug_flags;
extern const char *exportname;
extern bool foreground;
extern const char *ipaddr;
extern enum log_to log_to;
extern bool newstyle;
extern const char *port;
extern bool readonly;
extern const char *run;
extern const char *selinux_label;
extern int threads;
extern int tls;
extern const char *tls_certificates_dir;
extern const char *tls_psk;
extern bool tls_verify_peer;
extern char *unixsocket;
extern const char *user, *group;
extern bool verbose;

extern struct backend *backend;
#define for_each_backend(b) for (b = backend; b != NULL; b = b->next)

/* quit.c */
extern volatile int quit;
extern int quit_fd;
extern void set_up_quit_pipe (void);
extern void handle_quit (int sig);
extern void set_quit (void);

/* signals.c */
extern void set_up_signals (void);

/* background.c */
extern bool forked_into_background;
extern void fork_into_background (void);

/* captive.c */
extern void run_command (void);

/* socket-activation.c */
#define FIRST_SOCKET_ACTIVATION_FD 3 /* defined by systemd ABI */
extern unsigned int get_socket_activation (void);

/* usergroup.c */
extern void change_user (void);

/* connections.c */
struct connection;

typedef int (*connection_recv_function) (struct connection *,
                                         void *buf, size_t len)
  __attribute__((__nonnull__ (1, 2)));
typedef int (*connection_send_function) (struct connection *,
                                         const void *buf, size_t len)
  __attribute__((__nonnull__ (1, 2)));
typedef void (*connection_close_function) (struct connection *)
  __attribute__((__nonnull__ (1)));

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
  bool can_extents;
  bool using_tls;
  bool structured_replies;
  bool meta_context_base_allocation;

  int sockin, sockout;
  connection_recv_function recv;
  connection_send_function send;
  connection_close_function close;
};

extern int handle_single_connection (int sockin, int sockout);
extern int connection_set_handle (struct connection *conn,
                                  size_t i, void *handle)
  __attribute__((__nonnull__ (1 /* not 3 */)));
extern void *connection_get_handle (struct connection *conn, size_t i)
  __attribute__((__nonnull__ (1)));
extern int connection_get_status (struct connection *conn)
  __attribute__((__nonnull__ (1)));
extern int connection_set_status (struct connection *conn, int value)
  __attribute__((__nonnull__ (1)));

/* protocol-handshake.c */
extern int protocol_handshake (struct connection *conn)
  __attribute__((__nonnull__ (1)));
extern int protocol_compute_eflags (struct connection *conn, uint16_t *flags)
  __attribute__((__nonnull__ (1, 2)));

/* protocol-handshake-oldstyle.c */
extern int protocol_handshake_oldstyle (struct connection *conn)
  __attribute__((__nonnull__ (1)));

/* protocol-handshake-newstyle.c */
extern int protocol_handshake_newstyle (struct connection *conn)
  __attribute__((__nonnull__ (1)));

/* protocol.c */
extern int protocol_recv_request_send_reply (struct connection *conn)
  __attribute__((__nonnull__ (1)));

/* The context ID of base:allocation.  As far as I can tell it doesn't
 * matter what this is as long as nbdkit always returns the same
 * number.
 */
#define base_allocation_id 1

/* crypto.c */
#define root_tls_certificates_dir sysconfdir "/pki/" PACKAGE_NAME
extern void crypto_init (bool tls_set_on_cli);
extern void crypto_free (void);
extern int crypto_negotiate_tls (struct connection *conn,
                                 int sockin, int sockout)
  __attribute__((__nonnull__ (1)));

/* debug.c */
#define debug nbdkit_debug

/* log-*.c */
#if !HAVE_VFPRINTF_PERCENT_M
#include <stdio.h>
#define vfprintf nbdkit_vfprintf
extern int nbdkit_vfprintf (FILE *f, const char *fmt, va_list args)
  __attribute__((__format__ (printf, 2, 0)));
#endif
extern void log_stderr_verror (const char *fs, va_list args)
  __attribute__((__format__ (printf, 1, 0)));
extern void log_syslog_verror (const char *fs, va_list args)
  __attribute__((__format__ (printf, 1, 0)));

struct backend {
  /* Next filter or plugin in the chain.  This is always NULL for
   * plugins and never NULL for filters.
   */
  struct backend *next;

  /* A unique index used to fetch the handle from the connections
   * object.  The plugin (last in the chain) has index 0, and the
   * filters have index 1, 2, ... depending how "far" they are from
   * the plugin.
   */
  size_t i;

  void (*free) (struct backend *);
  int (*thread_model) (struct backend *);
  const char *(*name) (struct backend *);
  const char *(*plugin_name) (struct backend *);
  void (*usage) (struct backend *);
  const char *(*version) (struct backend *);
  void (*dump_fields) (struct backend *);
  void (*config) (struct backend *, const char *key, const char *value);
  void (*config_complete) (struct backend *);
  const char *(*magic_config_key) (struct backend *);
  int (*open) (struct backend *, struct connection *conn, int readonly);
  int (*prepare) (struct backend *, struct connection *conn);
  int (*finalize) (struct backend *, struct connection *conn);
  void (*close) (struct backend *, struct connection *conn);

  int64_t (*get_size) (struct backend *, struct connection *conn);
  int (*can_write) (struct backend *, struct connection *conn);
  int (*can_flush) (struct backend *, struct connection *conn);
  int (*is_rotational) (struct backend *, struct connection *conn);
  int (*can_trim) (struct backend *, struct connection *conn);
  int (*can_zero) (struct backend *, struct connection *conn);
  int (*can_extents) (struct backend *, struct connection *conn);
  int (*can_fua) (struct backend *, struct connection *conn);
  int (*can_multi_conn) (struct backend *, struct connection *conn);

  int (*pread) (struct backend *, struct connection *conn, void *buf,
                uint32_t count, uint64_t offset, uint32_t flags, int *err);
  int (*pwrite) (struct backend *, struct connection *conn, const void *buf,
                 uint32_t count, uint64_t offset, uint32_t flags, int *err);
  int (*flush) (struct backend *, struct connection *conn, uint32_t flags,
                int *err);
  int (*trim) (struct backend *, struct connection *conn, uint32_t count,
               uint64_t offset, uint32_t flags, int *err);
  int (*zero) (struct backend *, struct connection *conn, uint32_t count,
               uint64_t offset, uint32_t flags, int *err);
  int (*extents) (struct backend *, struct connection *conn, uint32_t count,
                  uint64_t offset, uint32_t flags,
                  struct nbdkit_extents *extents, int *err);
};

/* plugins.c */
extern struct backend *plugin_register (size_t index, const char *filename,
                                        void *dl, struct nbdkit_plugin *(*plugin_init) (void))
  __attribute__((__nonnull__ (2, 3, 4)));
extern void set_debug_flags (void *dl, const char *name)
  __attribute__((__nonnull__ (1, 2)));

/* filters.c */
extern struct backend *filter_register (struct backend *next, size_t index,
                                        const char *filename, void *dl,
                                        struct nbdkit_filter *(*filter_init) (void))
  __attribute__((__nonnull__ (1, 3, 4, 5)));

/* locks.c */
extern void lock_init_thread_model (void);
extern void lock_connection (void);
extern void unlock_connection (void);
extern void lock_request (struct connection *conn)
  __attribute__((__nonnull__ (1)));
extern void unlock_request (struct connection *conn)
  __attribute__((__nonnull__ (1)));
extern void lock_unload (void);
extern void unlock_unload (void);

/* sockets.c */
extern int *bind_unix_socket (size_t *)
  __attribute__((__nonnull__ (1)));
extern int *bind_tcpip_socket (size_t *)
  __attribute__((__nonnull__ (1)));
extern void accept_incoming_connections (int *socks, size_t nr_socks)
  __attribute__((__nonnull__ (1)));
extern void free_listening_sockets (int *socks, size_t nr_socks)
  __attribute__((__nonnull__ (1)));

/* threadlocal.c */
extern void threadlocal_init (void);
extern void threadlocal_new_server_thread (void);
extern void threadlocal_set_name (const char *name)
  __attribute__((__nonnull__ (1)));
extern const char *threadlocal_get_name (void);
extern void threadlocal_set_instance_num (size_t instance_num);
extern size_t threadlocal_get_instance_num (void);
extern void threadlocal_set_sockaddr (const struct sockaddr *addr,
                                      socklen_t addrlen)
  __attribute__((__nonnull__ (1)));
/*extern void threadlocal_get_sockaddr ();*/
extern void threadlocal_set_error (int err);
extern int threadlocal_get_error (void);
extern void *threadlocal_buffer (size_t size);

/* Declare program_name. */
#if HAVE_DECL_PROGRAM_INVOCATION_SHORT_NAME == 1
#include <errno.h>
#define program_name program_invocation_short_name
#else
#define program_name "nbdkit"
#endif

#endif /* NBDKIT_INTERNAL_H */
