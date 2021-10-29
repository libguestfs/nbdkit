/* nbdkit
 * Copyright (C) 2013-2021 Red Hat Inc.
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

#ifndef NBDKIT_VDDK_H
#define NBDKIT_VDDK_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

#include <pthread.h>

#include "isaligned.h"
#include "tvdiff.h"
#include "vector.h"

#include "vddk-structs.h"

enum compression_type { NONE = 0, ZLIB, FASTLZ, SKIPZ };

extern void *dl;
extern bool init_called;
extern __thread int error_suppression;
extern int library_version;
extern bool is_remote;

extern enum compression_type compression;
extern char *config;
extern const char *cookie;
extern const char *filename;
extern char *libdir;
extern uint16_t nfc_host_port;
extern char *password;
extern uint16_t port;
extern const char *server_name;
extern bool single_link;
extern const char *snapshot_moref;
extern const char *thumb_print;
extern const char *transport_modes;
extern bool unbuffered;
extern const char *username;
extern const char *vmx_spec;

extern int vddk_debug_diskinfo;
extern int vddk_debug_extents;
extern int vddk_debug_datapath;
extern int vddk_debug_stats;

#define STUB(fn,ret,args) extern ret (*fn) args
#define OPTIONAL_STUB(fn,ret,args) STUB(fn,ret,args)
#include "vddk-stubs.h"
#undef STUB
#undef OPTIONAL_STUB

/* Macros to bracket each VDDK API call, for printing debugging
 * information and collecting statistics.
 */
#define VDDK_CALL_START(fn, fs, ...)                                    \
  do {                                                                  \
  struct timeval start_t, end_t;                                        \
  /* GCC can optimize this away at compile time: */                     \
  const bool datapath =                                                 \
    strcmp (#fn, "VixDiskLib_Read") == 0 ||                             \
    strcmp (#fn, "VixDiskLib_ReadAsync") == 0 ||                        \
    strcmp (#fn, "VixDiskLib_Write") == 0 ||                            \
    strcmp (#fn, "VixDiskLib_WriteAsync") == 0;                         \
  if (vddk_debug_stats)                                                 \
    gettimeofday (&start_t, NULL);                                      \
  if (!datapath || vddk_debug_datapath)                                 \
    nbdkit_debug ("VDDK call: %s (" fs ")", #fn, ##__VA_ARGS__);        \
  do
#define VDDK_CALL_END(fn, bytes_)                       \
  while (0);                                            \
  if (vddk_debug_stats) {                               \
    gettimeofday (&end_t, NULL);                        \
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&stats_lock);       \
    stats_##fn.usecs += tvdiff_usec (&start_t, &end_t); \
    stats_##fn.calls++;                                 \
    stats_##fn.bytes += bytes_;                         \
  }                                                     \
  } while (0)

/* Print VDDK errors. */
#define VDDK_ERROR(err, fs, ...)                                \
  do {                                                          \
    char *vddk_err_msg;                                         \
    VDDK_CALL_START (VixDiskLib_GetErrorText, "%lu", err)       \
      vddk_err_msg = VixDiskLib_GetErrorText ((err), NULL);     \
    VDDK_CALL_END (VixDiskLib_GetErrorText, 0);                 \
    nbdkit_error (fs ": %s", ##__VA_ARGS__, vddk_err_msg);      \
    VDDK_CALL_START (VixDiskLib_FreeErrorText, "")              \
      VixDiskLib_FreeErrorText (vddk_err_msg);                  \
    VDDK_CALL_END (VixDiskLib_FreeErrorText, 0);                \
  } while (0)

/* Queue of asynchronous commands sent to the background thread. */
enum command_type { GET_SIZE, READ, WRITE, FLUSH, CAN_EXTENTS, EXTENTS, STOP };
struct command {
  /* These fields are set by the caller. */
  enum command_type type;       /* command */
  void *ptr;                    /* buffer, extents list, return values */
  uint32_t count;               /* READ, WRITE, EXTENTS */
  uint64_t offset;              /* READ, WRITE, EXTENTS */
  bool req_one;                 /* EXTENTS NBDKIT_FLAG_REQ_ONE */

  /* This field is set to a unique value by send_command_and_wait. */
  uint64_t id;                  /* serial number */

  /* These fields are used by the internal implementation. */
  pthread_mutex_t mutex;        /* completion mutex */
  pthread_cond_t cond;          /* completion condition */
  enum { SUBMITTED, SUCCEEDED, FAILED } status;
};

DEFINE_VECTOR_TYPE(command_queue, struct command *)

/* The per-connection handle. */
struct vddk_handle {
  VixDiskLibConnectParams *params; /* connection parameters */
  VixDiskLibConnection connection; /* connection */
  VixDiskLibHandle handle;         /* disk handle */

  pthread_t thread;                /* background thread for asynch work */

  /* Command queue of commands sent to the background thread.  Use
   * send_command_and_wait to add a command.  Only the background
   * thread must make VDDK API calls (apart from opening and closing).
   * The lock protects all of these fields.
   */
  pthread_mutex_t commands_lock;   /* lock */
  command_queue commands;          /* command queue */
  pthread_cond_t commands_cond;    /* condition (queue size 0 -> 1) */
  uint64_t id;                     /* next command ID */
};

/* reexec.c */
extern bool noreexec;
extern char *reexeced;
extern void reexec_if_needed (const char *prepend);
extern int restore_ld_library_path (void);

/* stats.c */
struct vddk_stat {
  const char *name;             /* function name */
  int64_t usecs;                /* total number of usecs consumed */
  uint64_t calls;               /* number of times called */
  uint64_t bytes;               /* bytes transferred, datapath calls only */
};
extern pthread_mutex_t stats_lock;
#define STUB(fn,ret,args) extern struct vddk_stat stats_##fn;
#define OPTIONAL_STUB(fn,ret,args) STUB(fn,ret,args)
#include "vddk-stubs.h"
#undef STUB
#undef OPTIONAL_STUB
extern void display_stats (void);

/* worker.c */
extern const char *command_type_string (enum command_type type);
extern int send_command_and_wait (struct vddk_handle *h, struct command *cmd);
extern void *vddk_worker_thread (void *handle);

#endif /* NBDKIT_VDDK_H */
