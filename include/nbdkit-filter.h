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

/* See nbdkit-filter(3) for documentation and how to write a filter. */

#ifndef NBDKIT_FILTER_H
#define NBDKIT_FILTER_H

#include <nbdkit-common.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NBDKIT_FILTER_API_VERSION 2

typedef int nbdkit_next_config (void *nxdata,
                                const char *key, const char *value);
typedef int nbdkit_next_config_complete (void *nxdata);
typedef int nbdkit_next_open (void *nxdata,
                              int readonly);

struct nbdkit_next_ops {
  int64_t (*get_size) (void *nxdata);

  int (*can_write) (void *nxdata);
  int (*can_flush) (void *nxdata);
  int (*is_rotational) (void *nxdata);
  int (*can_trim) (void *nxdata);

  int (*pread) (void *nxdata, void *buf, uint32_t count, uint64_t offset,
                uint32_t flags, int *err);
  int (*pwrite) (void *nxdata,
                 const void *buf, uint32_t count, uint64_t offset,
                 uint32_t flags, int *err);
  int (*flush) (void *nxdata, uint32_t flags, int *err);
  int (*trim) (void *nxdata, uint32_t count, uint64_t offset, uint32_t flags,
               int *err);
  int (*zero) (void *nxdata, uint32_t count, uint64_t offset, uint32_t flags,
               int *err);
};

struct nbdkit_filter {
  /* Do not set these fields directly; use NBDKIT_REGISTER_FILTER.
   * They exist so that we can diagnose filters compiled against
   * one version of the header with a runtime compiled against a
   * different version.
   */
  int _api_version;
  int _thread_model;

  /* Because there is no ABI guarantee, new fields may be added
   * where logically appropriate.  */
  const char *name;
  const char *longname;
  const char *version;
  const char *description;

  void (*load) (void);
  void (*unload) (void);

  int (*config) (nbdkit_next_config *next, void *nxdata,
                 const char *key, const char *value);
  int (*config_complete) (nbdkit_next_config_complete *next, void *nxdata);
  const char *config_help;

  void * (*open) (nbdkit_next_open *next, void *nxdata,
                  int readonly);
  void (*close) (void *handle);

  int (*prepare) (struct nbdkit_next_ops *next_ops, void *nxdata,
                  void *handle);
  int (*finalize) (struct nbdkit_next_ops *next_ops, void *nxdata,
                   void *handle);

  int64_t (*get_size) (struct nbdkit_next_ops *next_ops, void *nxdata,
                       void *handle);

  int (*can_write) (struct nbdkit_next_ops *next_ops, void *nxdata,
                    void *handle);
  int (*can_flush) (struct nbdkit_next_ops *next_ops, void *nxdata,
                    void *handle);
  int (*is_rotational) (struct nbdkit_next_ops *next_ops,
                        void *nxdata,
                        void *handle);
  int (*can_trim) (struct nbdkit_next_ops *next_ops, void *nxdata,
                   void *handle);

  int (*pread) (struct nbdkit_next_ops *next_ops, void *nxdata,
                void *handle, void *buf, uint32_t count, uint64_t offset,
                uint32_t flags, int *err);
  int (*pwrite) (struct nbdkit_next_ops *next_ops, void *nxdata,
                 void *handle,
                 const void *buf, uint32_t count, uint64_t offset,
                 uint32_t flags, int *err);
  int (*flush) (struct nbdkit_next_ops *next_ops, void *nxdata,
                void *handle, uint32_t flags, int *err);
  int (*trim) (struct nbdkit_next_ops *next_ops, void *nxdata,
               void *handle, uint32_t count, uint64_t offset, uint32_t flags,
               int *err);
  int (*zero) (struct nbdkit_next_ops *next_ops, void *nxdata,
               void *handle, uint32_t count, uint64_t offset, uint32_t flags,
               int *err);
};

#define NBDKIT_REGISTER_FILTER(filter)                                  \
  NBDKIT_CXX_LANG_C                                                     \
  struct nbdkit_filter *                                                \
  filter_init (void)                                                    \
  {                                                                     \
    (filter)._api_version = NBDKIT_FILTER_API_VERSION;                  \
    (filter)._thread_model = THREAD_MODEL;                              \
    return &(filter);                                                   \
  }

#ifdef __cplusplus
}
#endif

#endif /* NBDKIT_FILTER_H */
