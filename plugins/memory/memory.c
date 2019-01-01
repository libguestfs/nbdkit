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
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <nbdkit-plugin.h>

#include "sparse.h"

/* The size of disk in bytes (initialized by size=<SIZE> parameter). */
static int64_t size = 0;

/* Debug directory operations (-D memory.dir=1). */
int memory_debug_dir;

static struct sparse_array *sa;

static void
memory_load (void)
{
  sa = alloc_sparse_array (memory_debug_dir);
  if (sa == NULL) {
    perror ("malloc");
    exit (EXIT_FAILURE);
  }
}

static void
memory_unload (void)
{
  free_sparse_array (sa);
}

static int
memory_config (const char *key, const char *value)
{
  if (strcmp (key, "size") == 0) {
    size = nbdkit_parse_size (value);
    if (size == -1)
      return -1;
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
  return 0;
}

#define memory_config_help \
  "size=<SIZE>  (required) Size of the backing disk"

/* Create the per-connection handle. */
static void *
memory_open (int readonly)
{
  return NBDKIT_HANDLE_NOT_NEEDED;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

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
  sparse_array_read (sa, buf, count, offset);
  return 0;
}

/* Write data. */
static int
memory_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset)
{
  return sparse_array_write (sa, buf, count, offset);
}

/* Zero. */
static int
memory_zero (void *handle, uint32_t count, uint64_t offset, int may_trim)
{
  sparse_array_zero (sa, count, offset);
  return 0;
}

/* Trim (same as zero). */
static int
memory_trim (void *handle, uint32_t count, uint64_t offset)
{
  sparse_array_zero (sa, count, offset);
  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "memory",
  .version           = PACKAGE_VERSION,
  .load              = memory_load,
  .unload            = memory_unload,
  .config            = memory_config,
  .config_complete   = memory_config_complete,
  .config_help       = memory_config_help,
  .open              = memory_open,
  .get_size          = memory_get_size,
  .pread             = memory_pread,
  .pwrite            = memory_pwrite,
  .zero              = memory_zero,
  .trim              = memory_trim,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
