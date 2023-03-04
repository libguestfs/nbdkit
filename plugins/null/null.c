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
#include <errno.h>

#define NBDKIT_API_VERSION 2

#include <nbdkit-plugin.h>

/* The size of disk in bytes (initialized by size=<SIZE> parameter). */
static int64_t size = 0;

static int
null_config (const char *key, const char *value)
{
  int64_t r;

  if (strcmp (key, "size") == 0) {
    r = nbdkit_parse_size (value);
    if (r == -1)
      return -1;
    size = r;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

#define null_config_help \
  "size=<SIZE>             Size of the backing disk"

/* Create the per-connection handle. */
static void *
null_open (int readonly)
{
  return NBDKIT_HANDLE_NOT_NEEDED;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Get the disk size. */
static int64_t
null_get_size (void *handle)
{
  return size;
}

/* Serves the same data over multiple connections. */
static int
null_can_multi_conn (void *handle)
{
  return 1;
}

/* Cache. */
static int
null_can_cache (void *handle)
{
  /* Everything is already in memory, returning this without
   * implementing .cache lets nbdkit do the correct no-op.
   */
  return NBDKIT_CACHE_NATIVE;
}

/* Fast zero. */
static int
null_can_fast_zero (void *handle)
{
  return 1;
}

/* Read data. */
static int
null_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
            uint32_t flags)
{
  memset (buf, 0, count);
  return 0;
}

/* Write data. */
static int
null_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
             uint32_t flags)
{
  /* nothing */
  return 0;
}

/* Write zeroes. */
static int
null_zero (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  /* nothing */
  return 0;
}

/* Flush is a no-op, so advertise native FUA support */
static int
null_can_fua (void *handle)
{
  return NBDKIT_FUA_NATIVE;
}

/* Trim. */
static int
null_trim (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  /* nothing */
  return 0;
}

/* Nothing is persistent, so flush is trivially supported */
static int
null_flush (void *handle, uint32_t flags)
{
  return 0;
}

/* Extents. */
static int
null_extents (void *handle, uint32_t count, uint64_t offset, uint32_t flags,
              struct nbdkit_extents *extents)
{
  return nbdkit_add_extent (extents, 0, size,
                            NBDKIT_EXTENT_HOLE | NBDKIT_EXTENT_ZERO);
}

static struct nbdkit_plugin plugin = {
  .name              = "null",
  .version           = PACKAGE_VERSION,
  .config            = null_config,
  .config_help       = null_config_help,
  .magic_config_key  = "size",
  .open              = null_open,
  .get_size          = null_get_size,
  .can_multi_conn    = null_can_multi_conn,
  .can_cache         = null_can_cache,
  .can_fast_zero     = null_can_fast_zero,
  .pread             = null_pread,
  .pwrite            = null_pwrite,
  .zero              = null_zero,
  .trim              = null_trim,
  .can_fua           = null_can_fua,
  .flush             = null_flush,
  .extents           = null_extents,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN (plugin)
