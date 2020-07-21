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

/* See nbdkit-filter(3) for documentation and how to write a filter. */

#ifndef NBDKIT_FILTER_H
#define NBDKIT_FILTER_H

#include <stdlib.h>

#include <nbdkit-common.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NBDKIT_FILTER_API_VERSION 6 /* Corresponding to v1.16+ */

#define NBDKIT_ZERO_NONE     0
#define NBDKIT_ZERO_EMULATE  1
#define NBDKIT_ZERO_NATIVE   2

#ifdef NBDKIT_INTERNAL
/* Opaque type encapsulating all information needed for calling into
 * the next filter or plugin.
 */
typedef struct backend nbdkit_backend;
#else
typedef void nbdkit_backend;
#endif

/* Next ops. */
typedef int nbdkit_next_config (nbdkit_backend *nxdata,
                                const char *key, const char *value);
typedef int nbdkit_next_config_complete (nbdkit_backend *nxdata);
typedef int nbdkit_next_get_ready (nbdkit_backend *nxdata);
typedef int nbdkit_next_after_fork (nbdkit_backend *nxdata);
typedef int nbdkit_next_preconnect (nbdkit_backend *nxdata, int readonly);
typedef int nbdkit_next_open (nbdkit_backend *nxdata,
                              int readonly, const char *exportname);

struct nbdkit_next_ops {
  /* Performs close + open on the underlying chain.
   * Used by the retry filter.
   */
  int (*reopen) (nbdkit_backend *nxdata, int readonly, const char *exportname);

  /* The rest of the next ops are the same as normal plugin operations. */
  int64_t (*get_size) (nbdkit_backend *nxdata);

  int (*can_write) (nbdkit_backend *nxdata);
  int (*can_flush) (nbdkit_backend *nxdata);
  int (*is_rotational) (nbdkit_backend *nxdata);
  int (*can_trim) (nbdkit_backend *nxdata);
  int (*can_zero) (nbdkit_backend *nxdata);
  int (*can_fast_zero) (nbdkit_backend *nxdata);
  int (*can_extents) (nbdkit_backend *nxdata);
  int (*can_fua) (nbdkit_backend *nxdata);
  int (*can_multi_conn) (nbdkit_backend *nxdata);
  int (*can_cache) (nbdkit_backend *nxdata);

  int (*pread) (nbdkit_backend *nxdata,
                void *buf, uint32_t count, uint64_t offset,
                uint32_t flags, int *err);
  int (*pwrite) (nbdkit_backend *nxdata,
                 const void *buf, uint32_t count, uint64_t offset,
                 uint32_t flags, int *err);
  int (*flush) (nbdkit_backend *nxdata, uint32_t flags, int *err);
  int (*trim) (nbdkit_backend *nxdata, uint32_t count, uint64_t offset,
               uint32_t flags, int *err);
  int (*zero) (nbdkit_backend *nxdata, uint32_t count, uint64_t offset,
               uint32_t flags, int *err);
  int (*extents) (nbdkit_backend *nxdata, uint32_t count, uint64_t offset,
                  uint32_t flags, struct nbdkit_extents *extents, int *err);
  int (*cache) (nbdkit_backend *nxdata, uint32_t count, uint64_t offset,
                uint32_t flags, int *err);
};

/* Extent functions. */
struct nbdkit_extent {
  uint64_t offset;
  uint64_t length;
  uint32_t type;
};

extern struct nbdkit_extents *nbdkit_extents_new (uint64_t start, uint64_t end);
extern void nbdkit_extents_free (struct nbdkit_extents *);
extern size_t nbdkit_extents_count (const struct nbdkit_extents *);
extern struct nbdkit_extent nbdkit_get_extent (const struct nbdkit_extents *,
                                               size_t);
extern int nbdkit_extents_aligned (struct nbdkit_next_ops *next_ops,
                                   nbdkit_backend *nxdata,
                                   uint32_t count, uint64_t offset,
                                   uint32_t flags, uint32_t align,
                                   struct nbdkit_extents *extents, int *err);

/* Filter struct. */
struct nbdkit_filter {
  /* Do not set these fields directly; use NBDKIT_REGISTER_FILTER.
   * They exist so that we can diagnose filters compiled against one
   * version of the header with a runtime compiled against a different
   * version.  As of API version 6, _version is also part of the
   * guaranteed ABI, so we no longer have to remember to bump API
   * versions regardless of other API/ABI changes later in the struct.
   */
  int _api_version;
  const char *_version;

  /* Because there is no ABI guarantee, new fields may be added where
   * logically appropriate.
   */
  const char *name;
  const char *longname;
  const char *description;

  void (*load) (void);
  void (*unload) (void);

  int (*config) (nbdkit_next_config *next, nbdkit_backend *nxdata,
                 const char *key, const char *value);
  int (*config_complete) (nbdkit_next_config_complete *next,
                          nbdkit_backend *nxdata);
  const char *config_help;
  int (*thread_model) (void);
  int (*get_ready) (nbdkit_next_get_ready *next, nbdkit_backend *nxdata);
  int (*after_fork) (nbdkit_next_after_fork *next, nbdkit_backend *nxdata);
  int (*preconnect) (nbdkit_next_preconnect *next, nbdkit_backend *nxdata,
                     int readonly);

  void * (*open) (nbdkit_next_open *next, nbdkit_backend *nxdata,
                  int readonly, const char *exportname);
  void (*close) (void *handle);

  int (*prepare) (struct nbdkit_next_ops *next_ops, nbdkit_backend *nxdata,
                  void *handle, int readonly);
  int (*finalize) (struct nbdkit_next_ops *next_ops, nbdkit_backend *nxdata,
                   void *handle);

  int64_t (*get_size) (struct nbdkit_next_ops *next_ops, nbdkit_backend *nxdata,
                       void *handle);

  int (*can_write) (struct nbdkit_next_ops *next_ops, nbdkit_backend *nxdata,
                    void *handle);
  int (*can_flush) (struct nbdkit_next_ops *next_ops, nbdkit_backend *nxdata,
                    void *handle);
  int (*is_rotational) (struct nbdkit_next_ops *next_ops,
                        nbdkit_backend *nxdata, void *handle);
  int (*can_trim) (struct nbdkit_next_ops *next_ops, nbdkit_backend *nxdata,
                   void *handle);
  int (*can_zero) (struct nbdkit_next_ops *next_ops, nbdkit_backend *nxdata,
                   void *handle);
  int (*can_fast_zero) (struct nbdkit_next_ops *next_ops,
                        nbdkit_backend *nxdata, void *handle);
  int (*can_extents) (struct nbdkit_next_ops *next_ops, nbdkit_backend *nxdata,
                      void *handle);
  int (*can_fua) (struct nbdkit_next_ops *next_ops, nbdkit_backend *nxdata,
                  void *handle);
  int (*can_multi_conn) (struct nbdkit_next_ops *next_ops,
                         nbdkit_backend *nxdata, void *handle);
  int (*can_cache) (struct nbdkit_next_ops *next_ops, nbdkit_backend *nxdata,
                    void *handle);

  int (*pread) (struct nbdkit_next_ops *next_ops, nbdkit_backend *nxdata,
                void *handle, void *buf, uint32_t count, uint64_t offset,
                uint32_t flags, int *err);
  int (*pwrite) (struct nbdkit_next_ops *next_ops, nbdkit_backend *nxdata,
                 void *handle,
                 const void *buf, uint32_t count, uint64_t offset,
                 uint32_t flags, int *err);
  int (*flush) (struct nbdkit_next_ops *next_ops, nbdkit_backend *nxdata,
                void *handle, uint32_t flags, int *err);
  int (*trim) (struct nbdkit_next_ops *next_ops, nbdkit_backend *nxdata,
               void *handle, uint32_t count, uint64_t offset, uint32_t flags,
               int *err);
  int (*zero) (struct nbdkit_next_ops *next_ops, nbdkit_backend *nxdata,
               void *handle, uint32_t count, uint64_t offset, uint32_t flags,
               int *err);
  int (*extents) (struct nbdkit_next_ops *next_ops, nbdkit_backend *nxdata,
                  void *handle, uint32_t count, uint64_t offset, uint32_t flags,
                  struct nbdkit_extents *extents, int *err);
  int (*cache) (struct nbdkit_next_ops *next_ops, nbdkit_backend *nxdata,
                void *handle, uint32_t count, uint64_t offset, uint32_t flags,
                int *err);
};

#define NBDKIT_REGISTER_FILTER(filter)                                  \
  NBDKIT_CXX_LANG_C                                                     \
  struct nbdkit_filter *                                                \
  filter_init (void)                                                    \
  {                                                                     \
    (filter)._api_version = NBDKIT_FILTER_API_VERSION;                  \
    (filter)._version = NBDKIT_VERSION_STRING;                          \
    return &(filter);                                                   \
  }

#ifdef __cplusplus
}
#endif

#endif /* NBDKIT_FILTER_H */
