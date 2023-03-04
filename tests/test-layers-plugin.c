/* nbdkit
 * Copyright Red Hat
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
#include <string.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

#define DEBUG_FUNCTION nbdkit_debug ("%s", __func__)

static void
test_layers_plugin_load (void)
{
  DEBUG_FUNCTION;
}

static void
test_layers_plugin_unload (void)
{
  DEBUG_FUNCTION;
}

static int
test_layers_plugin_config (const char *key, const char *value)
{
  DEBUG_FUNCTION;
  return 0;
}

static int
test_layers_plugin_config_complete (void)
{
  DEBUG_FUNCTION;
  return 0;
}

#define test_layers_plugin_config_help "test_layers_plugin_config_help"

static int
test_layers_plugin_thread_model (void)
{
  DEBUG_FUNCTION;
  return NBDKIT_THREAD_MODEL_PARALLEL;
}

static int
test_layers_plugin_get_ready (void)
{
  DEBUG_FUNCTION;
  return 0;
}

static int
test_layers_plugin_after_fork (void)
{
  DEBUG_FUNCTION;
  return 0;
}

static void
test_layers_plugin_cleanup (void)
{
  DEBUG_FUNCTION;
}

static int
test_layers_plugin_preconnect (int readonly)
{
  DEBUG_FUNCTION;
  return 0;
}

static int
test_layers_plugin_list_exports (int readonly, int default_only,
                                 struct nbdkit_exports *exports)
{
  DEBUG_FUNCTION;
  return nbdkit_add_export (exports, "", NULL);
}

static const char *
test_layers_plugin_default_export (int readonly, int is_tls)
{
  DEBUG_FUNCTION;
  return "";
}

static void *
test_layers_plugin_open (int readonly)
{
  static int handle;

  DEBUG_FUNCTION;
  return &handle;
}

static void
test_layers_plugin_close (void *handle)
{
  DEBUG_FUNCTION;
}

static int64_t
test_layers_plugin_get_size (void *handle)
{
  DEBUG_FUNCTION;
  return 1024;
}

static int
test_layers_plugin_can_write (void *handle)
{
  DEBUG_FUNCTION;
  return 1;
}

static int
test_layers_plugin_can_flush (void *handle)
{
  DEBUG_FUNCTION;
  return 1;
}

static int
test_layers_plugin_is_rotational (void *handle)
{
  DEBUG_FUNCTION;
  return 1;
}

static int
test_layers_plugin_can_trim (void *handle)
{
  DEBUG_FUNCTION;
  return 1;
}

static int
test_layers_plugin_can_zero (void *handle)
{
  DEBUG_FUNCTION;
  return 1;
}

static int
test_layers_plugin_can_fast_zero (void *handle)
{
  DEBUG_FUNCTION;
  return 1;
}

static int
test_layers_plugin_can_fua (void *handle)
{
  DEBUG_FUNCTION;
  return NBDKIT_FUA_NATIVE;
}

static int
test_layers_plugin_can_multi_conn (void *handle)
{
  DEBUG_FUNCTION;
  return 1;
}

static int
test_layers_plugin_can_cache (void *handle)
{
  DEBUG_FUNCTION;
  return NBDKIT_CACHE_NATIVE;
}

static int
test_layers_plugin_can_extents (void *handle)
{
  DEBUG_FUNCTION;
  return 1;
}

static int
test_layers_plugin_pread (void *handle,
                          void *buf, uint32_t count, uint64_t offset,
                          uint32_t flags)
{
  DEBUG_FUNCTION;
  memset (buf, 0, count);
  return 0;
}

static int
test_layers_plugin_pwrite (void *handle,
                           const void *buf, uint32_t count, uint64_t offset,
                           uint32_t flags)
{
  DEBUG_FUNCTION;
  return 0;
}

static int
test_layers_plugin_flush (void *handle, uint32_t flags)
{
  DEBUG_FUNCTION;
  return 0;
}

static int
test_layers_plugin_trim (void *handle,
                         uint32_t count, uint64_t offset, uint32_t flags)
{
  DEBUG_FUNCTION;
  return 0;
}

static int
test_layers_plugin_zero (void *handle,
                         uint32_t count, uint64_t offset, uint32_t flags)
{
  DEBUG_FUNCTION;
  return 0;
}

static int
test_layers_plugin_extents (void *handle,
                            uint32_t count, uint64_t offset, uint32_t flags,
                            struct nbdkit_extents *extents)
{
  DEBUG_FUNCTION;
  return nbdkit_add_extent (extents, offset, count, 0);
}

static int
test_layers_plugin_cache (void *handle,
                         uint32_t count, uint64_t offset, uint32_t flags)
{
  DEBUG_FUNCTION;
  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "testlayersplugin",
  .version           = PACKAGE_VERSION,
  .load              = test_layers_plugin_load,
  .unload            = test_layers_plugin_unload,
  .config            = test_layers_plugin_config,
  .config_complete   = test_layers_plugin_config_complete,
  .config_help       = test_layers_plugin_config_help,
  .thread_model      = test_layers_plugin_thread_model,
  .get_ready         = test_layers_plugin_get_ready,
  .after_fork        = test_layers_plugin_after_fork,
  .cleanup           = test_layers_plugin_cleanup,
  .preconnect        = test_layers_plugin_preconnect,
  .list_exports      = test_layers_plugin_list_exports,
  .default_export    = test_layers_plugin_default_export,
  .open              = test_layers_plugin_open,
  .close             = test_layers_plugin_close,
  .get_size          = test_layers_plugin_get_size,
  .can_write         = test_layers_plugin_can_write,
  .can_flush         = test_layers_plugin_can_flush,
  .is_rotational     = test_layers_plugin_is_rotational,
  .can_trim          = test_layers_plugin_can_trim,
  .can_zero          = test_layers_plugin_can_zero,
  .can_fast_zero     = test_layers_plugin_can_fast_zero,
  .can_fua           = test_layers_plugin_can_fua,
  .can_multi_conn    = test_layers_plugin_can_multi_conn,
  .can_extents       = test_layers_plugin_can_extents,
  .can_cache         = test_layers_plugin_can_cache,
  .pread             = test_layers_plugin_pread,
  .pwrite            = test_layers_plugin_pwrite,
  .flush             = test_layers_plugin_flush,
  .trim              = test_layers_plugin_trim,
  .zero              = test_layers_plugin_zero,
  .extents           = test_layers_plugin_extents,
  .cache             = test_layers_plugin_cache,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN (plugin)
