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
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "byte-swapping.h"

/* The size of disk in bytes (initialized by size=<SIZE> parameter). */
static int64_t size = 0;

static int
pattern_config (const char *key, const char *value)
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

#define pattern_config_help \
  "size=<SIZE>  (required) Size of the backing disk"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Create the per-connection handle. */
static void *
pattern_open (int readonly)
{
  return NBDKIT_HANDLE_NOT_NEEDED;
}

/* Get the disk size. */
static int64_t
pattern_get_size (void *handle)
{
  return size;
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))

/* Read data. */
static int
pattern_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
               uint32_t flags)
{
  char *b = buf;
  uint64_t d;
  uint64_t o;
  uint32_t n;

  while (count > 0) {
    d = htobe64 (offset & ~7);
    o = offset & 7;
    n = MIN (count, 8-o);
    memcpy (b, (char *)&d + o, n);
    b += 8-o;
    offset += 8-o;
    count -= n;
  }

  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "pattern",
  .version           = PACKAGE_VERSION,
  .config            = pattern_config,
  .config_help       = pattern_config_help,
  .open              = pattern_open,
  .get_size          = pattern_get_size,
  .pread             = pattern_pread,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
