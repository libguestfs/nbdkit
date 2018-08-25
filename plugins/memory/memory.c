/* nbdkit
 * Copyright (C) 2017-2018 Red Hat Inc.
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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <nbdkit-plugin.h>

/* The size of disk in bytes (initialized by size=<SIZE> parameter). */
static size_t size = 0;
static void *disk;

static void
memory_unload (void)
{
  free (disk);
}

static int
memory_config (const char *key, const char *value)
{
  int64_t r;

  if (strcmp (key, "size") == 0) {
    r = nbdkit_parse_size (value);
    if (r == -1)
      return -1;
    if (r > SIZE_MAX) {
      nbdkit_error ("size > SIZE_MAX");
      return -1;
    }
    size = (ssize_t) r;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int
memory_config_complete (void)
{
  if (size == 0) {
    nbdkit_error ("you must specify size=<SIZE> on the command line");
    return -1;
  }
  disk = calloc (size, 1);
  if (disk == NULL) {
    nbdkit_error ("cannot allocate disk: %m");
    return -1;
  }
  return 0;
}

#define memory_config_help \
  "size=<SIZE>  (required) Size of the backing disk"

/* Create the per-connection handle. */
static void *
memory_open (int readonly)
{
  /* Used only as a handle pointer. */
  static int mh;

  return &mh;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Get the disk size. */
static int64_t
memory_get_size (void *handle)
{
  return (int64_t) size;
}

/* Read data. */
static int
memory_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  memcpy (buf, disk+offset, count);
  return 0;
}

/* Write data. */
static int
memory_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset)
{
  memcpy (disk+offset, buf, count);
  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "memory",
  .version           = PACKAGE_VERSION,
  .unload            = memory_unload,
  .config            = memory_config,
  .config_complete   = memory_config_complete,
  .config_help       = memory_config_help,
  .open              = memory_open,
  .get_size          = memory_get_size,
  .pread             = memory_pread,
  .pwrite            = memory_pwrite,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
