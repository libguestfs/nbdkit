/* nbdkit
 * Copyright (C) 2017-2018 Red Hat Inc.
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

#include "random.h"

/* The size of disk in bytes (initialized by size=<SIZE> parameter). */
static int64_t size = 0;

/* Seed. */
static uint32_t seed;

static void
random_load (void)
{
  /* Set the seed to a random-ish value.  This is not meant to be
   * cryptographically useful.  It can be overridden using the seed
   * parameter.
   */
  seed = time (NULL);
}

static int
random_config (const char *key, const char *value)
{
  int64_t r;

  if (strcmp (key, "seed") == 0) {
    if (sscanf (value, "%" SCNu32, &seed) != 1) {
      nbdkit_error ("could not parse seed parameter");
      return -1;
    }
  }
  else if (strcmp (key, "size") == 0) {
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

#define random_config_help \
  "size=<SIZE>  (required) Size of the backing disk\n" \
  "seed=<SEED>             Random number generator seed"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Create the per-connection handle. */
static void *
random_open (int readonly)
{
  return NBDKIT_HANDLE_NOT_NEEDED;
}

/* Get the disk size. */
static int64_t
random_get_size (void *handle)
{
  return size;
}

/* Serves the same data over multiple connections. */
static int
random_can_multi_conn (void *handle)
{
  return 1;
}

/* Read data. */
static int
random_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
              uint32_t flags)
{
  uint32_t i;
  unsigned char *b = buf;
  uint64_t s;

  for (i = 0; i < count; ++i) {
    /* We use nbdkit common/include/random.h to make random numbers.
     *
     * However we're not quite using it in the ordinary way.  In order
     * to be able to read any byte of data without needing to run the
     * PRNG from the start, the random data is computed from the index
     * and seed through three rounds of PRNG:
     *
     * index i     PRNG(seed+i)   -> PRNG -> PRNG -> mod 256 -> b[i]
     * index i+1   PRNG(seed+i+1) -> PRNG -> PRNG -> mod 256 -> b[i+1]
     * etc
     */
    struct random_state state;

    xsrandom (seed + offset + i, &state);
    xrandom (&state);
    xrandom (&state);
    s = xrandom (&state);
    s &= 255;
    b[i] = s;
  }
  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "random",
  .version           = PACKAGE_VERSION,
  .load              = random_load,
  .config            = random_config,
  .config_help       = random_config_help,
  .magic_config_key  = "size",
  .open              = random_open,
  .get_size          = random_get_size,
  .can_multi_conn    = random_can_multi_conn,
  .pread             = random_pread,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
