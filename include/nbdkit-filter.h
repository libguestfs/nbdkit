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
/* Opaque types encapsulating all information needed for calling into
 * the next filter or plugin.
 */
typedef struct backend nbdkit_backend;
typedef struct context nbdkit_context;
typedef struct context nbdkit_next;
#else
typedef struct nbdkit_backend nbdkit_backend;
typedef struct nbdkit_context nbdkit_context;
typedef struct nbdkit_next_ops nbdkit_next;
#endif

/* Next ops. */
typedef int nbdkit_next_config (nbdkit_backend *nxdata,
                                const char *key, const char *value);
typedef int nbdkit_next_config_complete (nbdkit_backend *nxdata);
typedef int nbdkit_next_preconnect (nbdkit_backend *nxdata, int readonly);
typedef int nbdkit_next_list_exports (nbdkit_backend *nxdata, int readonly,
                                      struct nbdkit_exports *exports);
typedef const char *nbdkit_next_default_export (nbdkit_backend *nxdata,
                                                int readonly);
typedef int nbdkit_next_open (nbdkit_context *context,
                              int readonly, const char *exportname);

struct nbdkit_next_ops {
  /* These callbacks are only needed when managing the backend manually
   * rather than via nbdkit_next_open.
   */
  int (*prepare) (nbdkit_next *nxdata);
  int (*finalize) (nbdkit_next *nxdata);

  /* These callbacks are the same as normal plugin operations. */
  int64_t (*get_size) (nbdkit_next *nxdata);
  const char * (*export_description) (nbdkit_next *nxdata);

  int (*can_write) (nbdkit_next *nxdata);
  int (*can_flush) (nbdkit_next *nxdata);
  int (*is_rotational) (nbdkit_next *nxdata);
  int (*can_trim) (nbdkit_next *nxdata);
  int (*can_zero) (nbdkit_next *nxdata);
  int (*can_fast_zero) (nbdkit_next *nxdata);
  int (*can_extents) (nbdkit_next *nxdata);
  int (*can_fua) (nbdkit_next *nxdata);
  int (*can_multi_conn) (nbdkit_next *nxdata);
  int (*can_cache) (nbdkit_next *nxdata);

  int (*pread) (nbdkit_next *nxdata,
                void *buf, uint32_t count, uint64_t offset,
                uint32_t flags, int *err);
  int (*pwrite) (nbdkit_next *nxdata,
                 const void *buf, uint32_t count, uint64_t offset,
                 uint32_t flags, int *err);
  int (*flush) (nbdkit_next *nxdata, uint32_t flags, int *err);
  int (*trim) (nbdkit_next *nxdata, uint32_t count, uint64_t offset,
               uint32_t flags, int *err);
  int (*zero) (nbdkit_next *nxdata, uint32_t count, uint64_t offset,
               uint32_t flags, int *err);
  int (*extents) (nbdkit_next *nxdata, uint32_t count, uint64_t offset,
                  uint32_t flags, struct nbdkit_extents *extents, int *err);
  int (*cache) (nbdkit_next *nxdata, uint32_t count, uint64_t offset,
                uint32_t flags, int *err);

  /* Note: Actual instances of this struct contain additional opaque
   * data not listed in this header; you cannot manually copy or
   * initialize sizeof(struct nbdkit_next_ops) bytes, but must instead
   * use unchanged pointers obtained from the nbdkit API.
   */
};

/* Extent functions. */
struct nbdkit_extent {
  uint64_t offset;
  uint64_t length;
  uint32_t type;
};

NBDKIT_EXTERN_DECL (struct nbdkit_extents *, nbdkit_extents_new,
                    (uint64_t start, uint64_t end));
NBDKIT_EXTERN_DECL (void, nbdkit_extents_free, (struct nbdkit_extents *));
NBDKIT_EXTERN_DECL (size_t, nbdkit_extents_count,
                    (const struct nbdkit_extents *));
NBDKIT_EXTERN_DECL (struct nbdkit_extent, nbdkit_get_extent,
                    (const struct nbdkit_extents *, size_t));
NBDKIT_EXTERN_DECL (struct nbdkit_extents *, nbdkit_extents_full,
                    (nbdkit_next *next,
                     uint32_t count, uint64_t offset,
                     uint32_t flags, int *err));
NBDKIT_EXTERN_DECL (int, nbdkit_extents_aligned,
                    (nbdkit_next *next,
                     uint32_t count, uint64_t offset,
                     uint32_t flags, uint32_t align,
                     struct nbdkit_extents *extents, int *err));

/* Export functions. */
struct nbdkit_export {
  char *name;
  char *description;
};

NBDKIT_EXTERN_DECL (struct nbdkit_exports *, nbdkit_exports_new,
                    (void));
NBDKIT_EXTERN_DECL (void, nbdkit_exports_free, (struct nbdkit_exports *));
NBDKIT_EXTERN_DECL (size_t, nbdkit_exports_count,
                    (const struct nbdkit_exports *));
NBDKIT_EXTERN_DECL (const struct nbdkit_export, nbdkit_get_export,
                    (const struct nbdkit_exports *, size_t));

/* Manual management of backend access. */
NBDKIT_EXTERN_DECL (nbdkit_backend *, nbdkit_context_get_backend,
                    (nbdkit_context *context));
NBDKIT_EXTERN_DECL (nbdkit_next *, nbdkit_next_context_open,
                    (nbdkit_backend *backend, int readonly,
                     const char *exportname, int shared));
NBDKIT_EXTERN_DECL (void, nbdkit_next_context_close, (nbdkit_next *next));
NBDKIT_EXTERN_DECL (nbdkit_next *, nbdkit_context_set_next,
                    (nbdkit_context *context, nbdkit_next *next));

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
  int (*get_ready) (int thread_model);
  int (*after_fork) (nbdkit_backend *backend);
  void (*cleanup) (nbdkit_backend *backend);
  int (*preconnect) (nbdkit_next_preconnect *next, nbdkit_backend *nxdata,
                     int readonly);
  int (*list_exports) (nbdkit_next_list_exports *next, nbdkit_backend *nxdata,
                       int readonly, int is_tls,
                       struct nbdkit_exports *exports);
  const char * (*default_export) (nbdkit_next_default_export *next,
                                  nbdkit_backend *nxdata,
                                  int readonly, int is_tls);

  void * (*open) (nbdkit_next_open *next, nbdkit_context *context,
                  int readonly, const char *exportname, int is_tls);
  void (*close) (void *handle);

  int (*prepare) (nbdkit_next *next,
                  void *handle, int readonly);
  int (*finalize) (nbdkit_next *next,
                   void *handle);

  int64_t (*get_size) (nbdkit_next *next,
                       void *handle);
  const char * (*export_description) (nbdkit_next *next, void *handle);

  int (*can_write) (nbdkit_next *next,
                    void *handle);
  int (*can_flush) (nbdkit_next *next,
                    void *handle);
  int (*is_rotational) (nbdkit_next *next, void *handle);
  int (*can_trim) (nbdkit_next *next,
                   void *handle);
  int (*can_zero) (nbdkit_next *next,
                   void *handle);
  int (*can_fast_zero) (nbdkit_next *next, void *handle);
  int (*can_extents) (nbdkit_next *next,
                      void *handle);
  int (*can_fua) (nbdkit_next *next,
                  void *handle);
  int (*can_multi_conn) (nbdkit_next *next, void *handle);
  int (*can_cache) (nbdkit_next *next,
                    void *handle);

  int (*pread) (nbdkit_next *next,
                void *handle, void *buf, uint32_t count, uint64_t offset,
                uint32_t flags, int *err);
  int (*pwrite) (nbdkit_next *next,
                 void *handle,
                 const void *buf, uint32_t count, uint64_t offset,
                 uint32_t flags, int *err);
  int (*flush) (nbdkit_next *next,
                void *handle, uint32_t flags, int *err);
  int (*trim) (nbdkit_next *next,
               void *handle, uint32_t count, uint64_t offset, uint32_t flags,
               int *err);
  int (*zero) (nbdkit_next *next,
               void *handle, uint32_t count, uint64_t offset, uint32_t flags,
               int *err);
  int (*extents) (nbdkit_next *next,
                  void *handle, uint32_t count, uint64_t offset, uint32_t flags,
                  struct nbdkit_extents *extents, int *err);
  int (*cache) (nbdkit_next *next,
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
