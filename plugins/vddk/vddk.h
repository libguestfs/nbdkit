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
#define OPTIONAL_STUB(fn,ret,args) extern ret (*fn) args
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
    strcmp (#fn, "VixDiskLib_Write") == 0;                              \
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

/* reexec.c */
extern bool noreexec;
extern char *reexeced;
extern void reexec_if_needed (const char *prepend);
extern int restore_ld_library_path (void);

#endif /* NBDKIT_VDDK_H */
