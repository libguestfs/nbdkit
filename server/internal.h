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

#ifndef NBDKIT_INTERNAL_H
#define NBDKIT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <pthread.h>

#define NBDKIT_API_VERSION 2
#define NBDKIT_INTERNAL
#include "nbdkit-plugin.h"
#include "nbdkit-filter.h"
#include "cleanup.h"
#include "nbd-protocol.h"

/* Define unlikely macro, but only for GCC.  These are used to move
 * debug and error handling code out of hot paths.
 */
#if defined(__GNUC__)
#define unlikely(x) __builtin_expect (!!(x), 0)
#define if_verbose if (unlikely (verbose))
#else
#define unlikely(x) (x)
#define if_verbose if (verbose)
#endif

#ifdef __APPLE__
#define UNIX_PATH_MAX 104
#else
#define UNIX_PATH_MAX 108
#endif

#if HAVE_VALGRIND
# include <valgrind.h>
/* http://valgrind.org/docs/manual/faq.html#faq.unhelpful */
# define DO_DLCLOSE !RUNNING_ON_VALGRIND
#elif defined(__SANITIZE_ADDRESS__)
# define DO_DLCLOSE 0
#elif ENABLE_LIBFUZZER
/* XXX This causes dlopen in the server to leak during fuzzing.
 * However it is necessary because of
 * https://bugs.llvm.org/show_bug.cgi?id=43917
 */
# define DO_DLCLOSE 0
#else
# define DO_DLCLOSE 1
#endif

#define container_of(ptr, type, member) ({                       \
      const typeof (((type *) 0)->member) *__mptr = (ptr);       \
      (type *) ((char *) __mptr - offsetof(type, member));       \
    })

/* Maximum read or write request that we will handle. */
#define MAX_REQUEST_SIZE (64 * 1024 * 1024)

/* main.c */
enum log_to {
  LOG_TO_DEFAULT,        /* --log not specified: log to stderr, unless
                            we forked into the background in which
                            case log to syslog */
  LOG_TO_STDERR,         /* --log=stderr forced on the command line */
  LOG_TO_SYSLOG,         /* --log=syslog forced on the command line */
  LOG_TO_NULL,           /* --log=null forced on the command line */
};

extern struct debug_flag *debug_flags;
extern const char *exportname;
extern bool foreground;
extern const char *ipaddr;
extern enum log_to log_to;
extern unsigned mask_handshake;
extern bool newstyle;
extern bool no_sr;
extern const char *port;
extern bool read_only;
extern const char *run;
extern bool listen_stdin;
extern const char *selinux_label;
extern unsigned threads;
extern int tls;
extern const char *tls_certificates_dir;
extern const char *tls_psk;
extern bool tls_verify_peer;
extern char *unixsocket;
extern const char *user, *group;
extern bool verbose;

/* Linked list of backends.  Each backend struct is followed by either
 * a filter or plugin struct.  "top" points to the first one.  They
 * are linked through the backend->next field.
 *
 *         ┌──────────┐    ┌──────────┐    ┌──────────┐
 * top ───▶│ backend  │───▶│ backend  │───▶│ backend  │
 *         │ b->i = 2 │    │ b->i = 1 │    │ b->i = 0 │
 *         │ filter   │    │ filter   │    │ plugin   │
 *         └──────────┘    └──────────┘    └──────────┘
 */
extern struct backend *top;
#define for_each_backend(b) for (b = top; b != NULL; b = b->next)

/* quit.c */
extern volatile int quit;
extern int quit_fd;
extern void set_up_quit_pipe (void);
extern void close_quit_pipe (void);
extern void handle_quit (int sig);

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

/* Flags for connection_send_function */
enum {
  SEND_MORE = 1, /* Hint to use MSG_MORE/corking to group send()s */
};

typedef int (*connection_recv_function) (void *buf, size_t len)
  __attribute__((__nonnull__ (1)));
typedef int (*connection_send_function) (const void *buf, size_t len,
                                         int flags)
  __attribute__((__nonnull__ (1)));
typedef void (*connection_close_function) (void);

/* struct handle stores data per connection and backend.  Primarily
 * this is the filter or plugin handle, but other state is also stored
 * here.
 *
 * Use get_handle (conn, 0) to return the struct handle for the
 * plugin, and get_handle (conn, b->i) to return the struct handle for
 * the i'th backend (if b->i >= 1 then for a filter).
 */
enum {
  HANDLE_OPEN = 1,      /* Set if .open passed, so .close is needed */
  HANDLE_CONNECTED = 2, /* Set if .prepare passed, so .finalize is needed */
  HANDLE_FAILED = 4,    /* Set if .finalize failed */
};

struct handle {
  void *handle;         /* Plugin or filter handle. */

  unsigned char state;  /* Bitmask of HANDLE_* values */

  uint64_t exportsize;
  int can_write;
  int can_flush;
  int is_rotational;
  int can_trim;
  int can_zero;
  int can_fast_zero;
  int can_fua;
  int can_multi_conn;
  int can_extents;
  int can_cache;
};

static inline void
reset_handle (struct handle *h)
{
  h->handle = NULL;
  h->state = 0;
  h->exportsize = -1;
  h->can_write = -1;
  h->can_flush = -1;
  h->is_rotational = -1;
  h->can_trim = -1;
  h->can_zero = -1;
  h->can_fast_zero = -1;
  h->can_fua = -1;
  h->can_multi_conn = -1;
  h->can_extents = -1;
  h->can_cache = -1;
}

struct connection {
  pthread_mutex_t request_lock;
  pthread_mutex_t read_lock;
  pthread_mutex_t write_lock;
  pthread_mutex_t status_lock;

  int status; /* 1 for more I/O with client, 0 for shutdown, -1 on error */
  int status_pipe[2]; /* track status changes via poll when nworkers > 1 */
  void *crypto_session;
  int nworkers;

  struct handle *handles;       /* One per plugin and filter. */
  size_t nr_handles;

  char exportname[NBD_MAX_STRING + 1];
  uint32_t exportnamelen;
  uint32_t cflags;
  uint16_t eflags;
  bool handshake_complete;
  bool using_tls;
  bool structured_replies;
  bool meta_context_base_allocation;

  int sockin, sockout;
  connection_recv_function recv;
  connection_send_function send;
  connection_close_function close;
};

static inline struct handle *
get_handle (struct connection *conn, int i)
{
  return &conn->handles[i];
}

extern void handle_single_connection (int sockin, int sockout);
extern int connection_get_status (void);
extern int connection_set_status (int value);

/* protocol-handshake.c */
extern int protocol_handshake (void);
extern int protocol_common_open (uint64_t *exportsize, uint16_t *flags)
  __attribute__((__nonnull__ (1, 2)));

/* protocol-handshake-oldstyle.c */
extern int protocol_handshake_oldstyle (void);

/* protocol-handshake-newstyle.c */
extern int protocol_handshake_newstyle (void);

/* protocol.c */
extern int protocol_recv_request_send_reply (void);

/* The context ID of base:allocation.  As far as I can tell it doesn't
 * matter what this is as long as nbdkit always returns the same
 * number.
 */
#define base_allocation_id 1

/* crypto.c */
#define root_tls_certificates_dir sysconfdir "/pki/" PACKAGE_NAME
extern void crypto_init (bool tls_set_on_cli);
extern void crypto_free (void);
extern int crypto_negotiate_tls (int sockin, int sockout);

/* debug.c */
#define debug(fs, ...)                                   \
  do {                                                   \
    if_verbose                                           \
      nbdkit_debug ((fs), ##__VA_ARGS__);                \
  } while (0)

/* debug-flags.c */
extern void add_debug_flag (const char *arg);
extern void apply_debug_flags (void *dl, const char *name);
extern void free_debug_flags (void);

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

/* backend.c */
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

  /* The type of backend: filter or plugin. */
  const char *type;

  /* A copy of the backend name that survives a dlclose. */
  char *name;

  /* The file the backend was loaded from. */
  char *filename;

  /* The dlopen handle for the backend. */
  void *dl;

  /* Backend callbacks. All are required. */
  void (*free) (struct backend *);
  int (*thread_model) (struct backend *);
  const char *(*plugin_name) (struct backend *);
  void (*usage) (struct backend *);
  const char *(*version) (struct backend *);
  void (*dump_fields) (struct backend *);
  void (*config) (struct backend *, const char *key, const char *value);
  void (*config_complete) (struct backend *);
  const char *(*magic_config_key) (struct backend *);
  void (*get_ready) (struct backend *);
  int (*preconnect) (struct backend *, int readonly);
  void *(*open) (struct backend *, int readonly);
  int (*prepare) (struct backend *, void *handle, int readonly);
  int (*finalize) (struct backend *, void *handle);
  void (*close) (struct backend *, void *handle);

  int64_t (*get_size) (struct backend *, void *handle);
  int (*can_write) (struct backend *, void *handle);
  int (*can_flush) (struct backend *, void *handle);
  int (*is_rotational) (struct backend *, void *handle);
  int (*can_trim) (struct backend *, void *handle);
  int (*can_zero) (struct backend *, void *handle);
  int (*can_fast_zero) (struct backend *, void *handle);
  int (*can_extents) (struct backend *, void *handle);
  int (*can_fua) (struct backend *, void *handle);
  int (*can_multi_conn) (struct backend *, void *handle);
  int (*can_cache) (struct backend *, void *handle);

  int (*pread) (struct backend *, void *handle,
                void *buf, uint32_t count, uint64_t offset,
                uint32_t flags, int *err);
  int (*pwrite) (struct backend *, void *handle,
                 const void *buf, uint32_t count, uint64_t offset,
                 uint32_t flags, int *err);
  int (*flush) (struct backend *, void *handle, uint32_t flags, int *err);
  int (*trim) (struct backend *, void *handle,
               uint32_t count, uint64_t offset, uint32_t flags, int *err);
  int (*zero) (struct backend *, void *handle,
               uint32_t count, uint64_t offset, uint32_t flags, int *err);
  int (*extents) (struct backend *, void *handle,
                  uint32_t count, uint64_t offset, uint32_t flags,
                  struct nbdkit_extents *extents, int *err);
  int (*cache) (struct backend *, void *handle,
                uint32_t count, uint64_t offset, uint32_t flags, int *err);
};

extern void backend_init (struct backend *b, struct backend *next, size_t index,
                          const char *filename, void *dl, const char *type)
  __attribute__((__nonnull__ (1, 4, 5, 6)));
extern void backend_load (struct backend *b, const char *name,
                          void (*load) (void))
  __attribute__((__nonnull__ (1 /* not 2 */)));
extern void backend_unload (struct backend *b, void (*unload) (void))
  __attribute__((__nonnull__ (1)));

extern int backend_open (struct backend *b, int readonly)
  __attribute__((__nonnull__ (1)));
extern int backend_prepare (struct backend *b)
  __attribute__((__nonnull__ (1)));
extern int backend_finalize (struct backend *b)
  __attribute__((__nonnull__ (1)));
extern void backend_close (struct backend *b)
  __attribute__((__nonnull__ (1)));
extern bool backend_valid_range (struct backend *b,
                                 uint64_t offset, uint32_t count)
  __attribute__((__nonnull__ (1)));

extern int backend_reopen (struct backend *b, int readonly)
  __attribute__((__nonnull__ (1)));
extern int64_t backend_get_size (struct backend *b)
  __attribute__((__nonnull__ (1)));
extern int backend_can_write (struct backend *b)
  __attribute__((__nonnull__ (1)));
extern int backend_can_flush (struct backend *b)
  __attribute__((__nonnull__ (1)));
extern int backend_is_rotational (struct backend *b)
  __attribute__((__nonnull__ (1)));
extern int backend_can_trim (struct backend *b)
  __attribute__((__nonnull__ (1)));
extern int backend_can_zero (struct backend *b)
  __attribute__((__nonnull__ (1)));
extern int backend_can_fast_zero (struct backend *b)
  __attribute__((__nonnull__ (1)));
extern int backend_can_extents (struct backend *b)
  __attribute__((__nonnull__ (1)));
extern int backend_can_fua (struct backend *b)
  __attribute__((__nonnull__ (1)));
extern int backend_can_multi_conn (struct backend *b)
  __attribute__((__nonnull__ (1)));
extern int backend_can_cache (struct backend *b)
  __attribute__((__nonnull__ (1)));

extern int backend_pread (struct backend *b,
                          void *buf, uint32_t count, uint64_t offset,
                          uint32_t flags, int *err)
  __attribute__((__nonnull__ (1, 2, 6)));
extern int backend_pwrite (struct backend *b,
                           const void *buf, uint32_t count, uint64_t offset,
                           uint32_t flags, int *err)
  __attribute__((__nonnull__ (1, 2, 6)));
extern int backend_flush (struct backend *b,
                          uint32_t flags, int *err)
  __attribute__((__nonnull__ (1, 3)));
extern int backend_trim (struct backend *b,
                         uint32_t count, uint64_t offset, uint32_t flags,
                         int *err)
  __attribute__((__nonnull__ (1, 5)));
extern int backend_zero (struct backend *b,
                         uint32_t count, uint64_t offset, uint32_t flags,
                         int *err)
  __attribute__((__nonnull__ (1, 5)));
extern int backend_extents (struct backend *b,
                            uint32_t count, uint64_t offset, uint32_t flags,
                            struct nbdkit_extents *extents, int *err)
  __attribute__((__nonnull__ (1, 5, 6)));
extern int backend_cache (struct backend *b,
                          uint32_t count, uint64_t offset,
                          uint32_t flags, int *err)
  __attribute__((__nonnull__ (1, 5)));

/* plugins.c */
extern struct backend *plugin_register (size_t index, const char *filename,
                                        void *dl, struct nbdkit_plugin *(*plugin_init) (void))
  __attribute__((__nonnull__ (2, 3, 4)));

/* filters.c */
extern struct backend *filter_register (struct backend *next, size_t index,
                                        const char *filename, void *dl,
                                        struct nbdkit_filter *(*filter_init) (void))
  __attribute__((__nonnull__ (1, 3, 4, 5)));

/* locks.c */
extern unsigned thread_model;
extern void lock_init_thread_model (void);
extern const char *name_of_thread_model (int model);
extern void lock_connection (void);
extern void unlock_connection (void);
extern void lock_request (void);
extern void unlock_request (void);
extern void lock_unload (void);
extern void unlock_unload (void);

/* sockets.c */
extern int *bind_unix_socket (size_t *)
  __attribute__((__nonnull__ (1)));
extern int *bind_tcpip_socket (size_t *)
  __attribute__((__nonnull__ (1)));
extern int *bind_vsock (size_t *)
  __attribute__((__nonnull__ (1)));
extern void accept_incoming_connections (int *socks, size_t nr_socks)
  __attribute__((__nonnull__ (1)));

/* threadlocal.c */
extern void threadlocal_init (void);
extern void threadlocal_new_server_thread (void);
extern void threadlocal_set_name (const char *name)
  __attribute__((__nonnull__ (1)));
extern const char *threadlocal_get_name (void);
extern void threadlocal_set_instance_num (size_t instance_num);
extern size_t threadlocal_get_instance_num (void);
extern void threadlocal_set_error (int err);
extern int threadlocal_get_error (void);
extern void *threadlocal_buffer (size_t size);
extern void threadlocal_set_conn (struct connection *conn);
extern struct connection *threadlocal_get_conn (void);

/* Macro which sets local variable struct connection *conn from
 * thread-local storage, asserting that it is non-NULL.  If you want
 * to check if conn could be NULL (eg. outside a connection context)
 * then call threadlocal_get_conn instead.
 */
#define GET_CONN                                        \
  struct connection *conn = threadlocal_get_conn ();    \
  assert (conn != NULL)

/* Declare program_name. */
#if HAVE_DECL_PROGRAM_INVOCATION_SHORT_NAME == 1
#include <errno.h>
#define program_name program_invocation_short_name
#else
#define program_name "nbdkit"
#endif

#endif /* NBDKIT_INTERNAL_H */
