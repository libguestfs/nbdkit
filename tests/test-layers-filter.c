/* nbdkit
 * Copyright (C) 2018-2019 Red Hat Inc.
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
#include <stdint.h>

#include <nbdkit-filter.h>

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

#define str(s) #s
#define DEBUG_FUNCTION nbdkit_debug ("%s: %s", layer, __func__)

static void
test_layers_filter_load (void)
{
  DEBUG_FUNCTION;
}

static void
test_layers_filter_unload (void)
{
  DEBUG_FUNCTION;
}

static int
test_layers_filter_config (nbdkit_next_config *next, void *nxdata,
                            const char *key, const char *value)
{
  DEBUG_FUNCTION;
  return next (nxdata, key, value);
}

static int
test_layers_filter_config_complete (nbdkit_next_config_complete *next,
                                     void *nxdata)
{
  DEBUG_FUNCTION;
  return next (nxdata);
}

#define test_layers_filter_config_help          \
  "test_layers_" layer "_config_help"

static void *
test_layers_filter_open (nbdkit_next_open *next, void *nxdata, int readonly)
{
  static int handle;

  DEBUG_FUNCTION;

  if (next (nxdata, readonly) == -1)
    return NULL;

  return &handle;
}

static void
test_layers_filter_close (void *handle)
{
  DEBUG_FUNCTION;
}

static int
test_layers_filter_prepare (struct nbdkit_next_ops *next_ops, void *nxdata,
                             void *handle)
{
  DEBUG_FUNCTION;
  return 0;
}

static int
test_layers_filter_finalize (struct nbdkit_next_ops *next_ops, void *nxdata,
                              void *handle)
{
  DEBUG_FUNCTION;
  return 0;
}

static int64_t
test_layers_filter_get_size (struct nbdkit_next_ops *next_ops, void *nxdata,
                              void *handle)
{
  DEBUG_FUNCTION;
  return next_ops->get_size (nxdata);
}

static int
test_layers_filter_can_write (struct nbdkit_next_ops *next_ops, void *nxdata,
                               void *handle)
{
  DEBUG_FUNCTION;
  return next_ops->can_write (nxdata);
}

static int
test_layers_filter_can_flush (struct nbdkit_next_ops *next_ops, void *nxdata,
                               void *handle)
{
  DEBUG_FUNCTION;
  return next_ops->can_flush (nxdata);
}

static int
test_layers_filter_is_rotational (struct nbdkit_next_ops *next_ops,
                                   void *nxdata,
                                   void *handle)
{
  DEBUG_FUNCTION;
  return next_ops->is_rotational (nxdata);
}

static int
test_layers_filter_can_trim (struct nbdkit_next_ops *next_ops, void *nxdata,
                              void *handle)
{
  DEBUG_FUNCTION;
  return next_ops->can_trim (nxdata);
}

static int
test_layers_filter_can_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
                              void *handle)
{
  DEBUG_FUNCTION;
  return next_ops->can_zero (nxdata);
}

static int
test_layers_filter_can_fua (struct nbdkit_next_ops *next_ops, void *nxdata,
                             void *handle)
{
  DEBUG_FUNCTION;
  return next_ops->can_fua (nxdata);
}

static int
test_layers_filter_can_multi_conn (struct nbdkit_next_ops *next_ops,
                                   void *nxdata,
                                   void *handle)
{
  DEBUG_FUNCTION;
  return next_ops->can_multi_conn (nxdata);
}

static int
test_layers_filter_can_extents (struct nbdkit_next_ops *next_ops,
                                void *nxdata,
                                void *handle)
{
  DEBUG_FUNCTION;
  return next_ops->can_extents (nxdata);
}

static int
test_layers_filter_can_cache (struct nbdkit_next_ops *next_ops,
                              void *nxdata,
                              void *handle)
{
  DEBUG_FUNCTION;
  return next_ops->can_cache (nxdata);
}

static int
test_layers_filter_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
                           void *handle, void *buf,
                           uint32_t count, uint64_t offset,
                           uint32_t flags, int *err)
{
  DEBUG_FUNCTION;
  return next_ops->pread (nxdata, buf, count, offset, flags, err);
}

static int
test_layers_filter_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
                            void *handle,
                            const void *buf, uint32_t count, uint64_t offset,
                            uint32_t flags, int *err)
{
  DEBUG_FUNCTION;
  return next_ops->pwrite (nxdata, buf, count, offset, flags, err);
}

static int
test_layers_filter_flush (struct nbdkit_next_ops *next_ops, void *nxdata,
                           void *handle,
                           uint32_t flags, int *err)
{
  DEBUG_FUNCTION;
  return next_ops->flush (nxdata, flags, err);
}

static int
test_layers_filter_trim (struct nbdkit_next_ops *next_ops, void *nxdata,
                          void *handle, uint32_t count, uint64_t offset,
                          uint32_t flags, int *err)
{
  DEBUG_FUNCTION;
  return next_ops->trim (nxdata, count, offset, flags, err);
}

static int
test_layers_filter_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
                          void *handle, uint32_t count, uint64_t offset,
                          uint32_t flags, int *err)
{
  DEBUG_FUNCTION;
  return next_ops->zero (nxdata, count, offset, flags, err);
}

static int
test_layers_filter_extents (struct nbdkit_next_ops *next_ops, void *nxdata,
                            void *handle, uint32_t count, uint64_t offset,
                            uint32_t flags, struct nbdkit_extents *extents,
                            int *err)
{
  DEBUG_FUNCTION;
  return next_ops->extents (nxdata, count, offset, flags, extents, err);
}

static int
test_layers_filter_cache (struct nbdkit_next_ops *next_ops, void *nxdata,
                          void *handle, uint32_t count, uint64_t offset,
                          uint32_t flags, int *err)
{
  DEBUG_FUNCTION;
  return next_ops->cache (nxdata, count, offset, flags, err);
}

static struct nbdkit_filter filter = {
  .name              = "testlayers" layer,
  .load              = test_layers_filter_load,
  .unload            = test_layers_filter_unload,
  .config            = test_layers_filter_config,
  .config_complete   = test_layers_filter_config_complete,
  .config_help       = test_layers_filter_config_help,
  .open              = test_layers_filter_open,
  .close             = test_layers_filter_close,
  .prepare           = test_layers_filter_prepare,
  .finalize          = test_layers_filter_finalize,
  .get_size          = test_layers_filter_get_size,
  .can_write         = test_layers_filter_can_write,
  .can_flush         = test_layers_filter_can_flush,
  .is_rotational     = test_layers_filter_is_rotational,
  .can_trim          = test_layers_filter_can_trim,
  .can_zero          = test_layers_filter_can_zero,
  .can_fua           = test_layers_filter_can_fua,
  .can_multi_conn    = test_layers_filter_can_multi_conn,
  .can_extents       = test_layers_filter_can_extents,
  .can_cache         = test_layers_filter_can_cache,
  .pread             = test_layers_filter_pread,
  .pwrite            = test_layers_filter_pwrite,
  .flush             = test_layers_filter_flush,
  .trim              = test_layers_filter_trim,
  .zero              = test_layers_filter_zero,
  .extents           = test_layers_filter_extents,
  .cache             = test_layers_filter_cache,
};

NBDKIT_REGISTER_FILTER(filter)
