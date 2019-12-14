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

/* See nbdkit-plugin(3) for documentation and how to write a plugin. */

#ifndef NBDKIT_PLUGIN_H
#define NBDKIT_PLUGIN_H

#include <nbdkit-common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* By default, a plugin gets API version 1; but you may request
 * version 2 prior to including this header */
#ifndef NBDKIT_API_VERSION
#define NBDKIT_API_VERSION                            1
#elif (NBDKIT_API_VERSION - 0) < 1 || NBDKIT_API_VERSION > 2
#error Unsupported API version
#endif

struct nbdkit_plugin {
  /* Do not set these fields directly; use NBDKIT_REGISTER_PLUGIN.
   * They exist so that we can support plugins compiled against
   * one version of the header with a runtime compiled against a
   * different version with more (or fewer) fields.
   */
  uint64_t _struct_size;
  int _api_version;
  int _thread_model;

  /* Plugins are responsible for these fields; see the documentation
   * for semantics, and which fields are optional. New fields will
   * only be added at the end of the struct.
   */
  const char *name;
  const char *longname;
  const char *version;
  const char *description;

  void (*load) (void);
  void (*unload) (void);

  int (*config) (const char *key, const char *value);
  int (*config_complete) (void);
  const char *config_help;

  void * (*open) (int readonly);
  void (*close) (void *handle);

  int64_t (*get_size) (void *handle);

  int (*can_write) (void *handle);
  int (*can_flush) (void *handle);
  int (*is_rotational) (void *handle);
  int (*can_trim) (void *handle);

#if NBDKIT_API_VERSION == 1
  int (*pread) (void *handle, void *buf, uint32_t count, uint64_t offset);
  int (*pwrite) (void *handle, const void *buf, uint32_t count, uint64_t offset);
  int (*flush) (void *handle);
  int (*trim) (void *handle, uint32_t count, uint64_t offset);
  int (*zero) (void *handle, uint32_t count, uint64_t offset, int may_trim);
#else
  int (*_pread_old) (void *, void *, uint32_t, uint64_t);
  int (*_pwrite_old) (void *, const void *, uint32_t, uint64_t);
  int (*_flush_old) (void *);
  int (*_trim_old) (void *, uint32_t, uint64_t);
  int (*_zero_old) (void *, uint32_t, uint64_t, int);
#endif

  int errno_is_preserved;

  void (*dump_plugin) (void);

  int (*can_zero) (void *handle);
  int (*can_fua) (void *handle);
#if NBDKIT_API_VERSION == 1
  int (*_unused1) (void *, void *, uint32_t, uint64_t, uint32_t);
  int (*_unused2) (void *, const void *, uint32_t, uint64_t, uint32_t);
  int (*_unused3) (void *, uint32_t);
  int (*_unused4) (void *, uint32_t, uint64_t, uint32_t);
  int (*_unused5) (void *, uint32_t, uint64_t, uint32_t);
#else
  int (*pread) (void *handle, void *buf, uint32_t count, uint64_t offset,
                uint32_t flags);
  int (*pwrite) (void *handle, const void *buf, uint32_t count,
                 uint64_t offset, uint32_t flags);
  int (*flush) (void *handle, uint32_t flags);
  int (*trim) (void *handle, uint32_t count, uint64_t offset, uint32_t flags);
  int (*zero) (void *handle, uint32_t count, uint64_t offset, uint32_t flags);
#endif

  const char *magic_config_key;

  int (*can_multi_conn) (void *handle);

  int (*can_extents) (void *handle);
  int (*extents) (void *handle, uint32_t count, uint64_t offset, uint32_t flags,
                  struct nbdkit_extents *extents);
  int (*can_cache) (void *handle);
  int (*cache) (void *handle, uint32_t count, uint64_t offset, uint32_t flags);

  int (*thread_model) (void);

  int (*can_fast_zero) (void *handle);

  int (*preconnect) (int readonly);
};

extern void nbdkit_set_error (int err);

#define NBDKIT_REGISTER_PLUGIN(plugin)                                  \
  NBDKIT_CXX_LANG_C                                                     \
  struct nbdkit_plugin *                                                \
  plugin_init (void)                                                    \
  {                                                                     \
    (plugin)._struct_size = sizeof (plugin);                            \
    (plugin)._api_version = NBDKIT_API_VERSION;                         \
    (plugin)._thread_model = THREAD_MODEL;                              \
    return &(plugin);                                                   \
  }

#ifdef __cplusplus
}
#endif

#endif /* NBDKIT_PLUGIN_H */
