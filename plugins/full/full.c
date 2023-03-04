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
static int64_t size = -1;

static int
full_config (const char *key, const char *value)
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

static int
full_config_complete (void)
{
  if (size == -1) {
    nbdkit_error ("size parameter is required");
    return -1;
  }

  return 0;
}

#define full_config_help \
  "size=<SIZE>  (required) Size of the backing disk"

/* Create the per-connection handle. */
static void *
full_open (int readonly)
{
  return NBDKIT_HANDLE_NOT_NEEDED;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Get the disk size. */
static int64_t
full_get_size (void *handle)
{
  return size;
}

/* Serves the same data over multiple connections. */
static int
full_can_multi_conn (void *handle)
{
  return 1;
}

/* Cache. */
static int
full_can_cache (void *handle)
{
  /* Everything is already in memory, returning this without
   * implementing .cache lets nbdkit do the correct no-op.
   */
  return NBDKIT_CACHE_NATIVE;
}

/* Read data. */
static int
full_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
            uint32_t flags)
{
  memset (buf, 0, count);
  return 0;
}

/* Write data. */
static int
full_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
             uint32_t flags)
{
  errno = ENOSPC;
  return -1;
}

/* Omitting full_zero is intentional: that way, nbdkit defaults to
 * permitting fast zeroes which respond with ENOTSUP, while normal
 * zeroes fall back to pwrite and respond with ENOSPC.
 */

/* Trim. */
static int
full_trim (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  errno = ENOSPC;
  return -1;
}

/* Extents. */
static int
full_extents (void *handle, uint32_t count, uint64_t offset, uint32_t flags,
              struct nbdkit_extents *extents)
{
  return nbdkit_add_extent (extents, 0, size,
                            NBDKIT_EXTENT_HOLE | NBDKIT_EXTENT_ZERO);
}

/* Note that we don't need to handle flush: If there has been previous
 * write then we have already returned an error.  If there have been
 * no previous writes then flush can be ignored.
 */

static struct nbdkit_plugin plugin = {
  .name              = "full",
  .version           = PACKAGE_VERSION,
  .config            = full_config,
  .config_complete   = full_config_complete,
  .config_help       = full_config_help,
  .magic_config_key  = "size",
  .open              = full_open,
  .get_size          = full_get_size,
  .can_multi_conn    = full_can_multi_conn,
  .can_cache         = full_can_cache,
  .pread             = full_pread,
  .pwrite            = full_pwrite,
  .trim              = full_trim,
  .extents           = full_extents,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN (plugin)
