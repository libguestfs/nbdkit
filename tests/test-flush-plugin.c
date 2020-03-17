/* nbdkit
 * Copyright (C) 2013-2020 Red Hat Inc.
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
#include <string.h>

#define NBDKIT_API_VERSION 2

#include <nbdkit-plugin.h>

/* level abuses our knowledge of internal nbdkit values:
 *  -1: force error during connect
 *   0: no flush, no FUA
 *   1: flush works, FUA is emulated
 *   2: flush works, FUA is native
 */
static int level = -1;

static int
flush_config (const char *key, const char *value)
{
  if (strcmp (key, "level") == 0)
    return nbdkit_parse_int (key, value, &level);
  nbdkit_error ("unknown parameter '%s'", key);
  return -1;
}

/* Implements both .can_flush and .can_fua */
static int
flush_level (void *handle)
{
  return level;
}

static void *
flush_open (int readonly)
{
  return NBDKIT_HANDLE_NOT_NEEDED;
}

static int64_t
flush_get_size (void *handle)
{
  return 1024*1024;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

static int
flush_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
             uint32_t flags)
{
  memset (buf, 0, count);
  return 0;
}

static int
flush_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
              uint32_t flags)
{
  if (flags & NBDKIT_FLAG_FUA)
    nbdkit_debug (" **handling native FUA");
  return 0;
}

static int
flush_flush (void *handle, uint32_t flags)
{
  nbdkit_debug (" **handling flush");
  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "flush",
  .version           = PACKAGE_VERSION,
  .config            = flush_config,
  .magic_config_key  = "level",
  .open              = flush_open,
  .get_size          = flush_get_size,
  .pread             = flush_pread,
  .pwrite            = flush_pwrite,
  .can_flush         = flush_level,
  .can_fua           = flush_level,
  .flush             = flush_flush,
};

NBDKIT_REGISTER_PLUGIN(plugin)
