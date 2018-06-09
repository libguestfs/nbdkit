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

/* The size of disk in bytes (initialized by size=<SIZE> parameter). */
static size_t size = 0;

/* Seed. */
static uint32_t seed;

/* We use a Linear Congruential Generator (LCG) to make low quality
 * but easy to compute random numbers.  However we're not quite using
 * it in the ordinary way.  In order to be able to read any byte of
 * data without needing to run the LCG from the start, the random data
 * is computed from the index and seed through two rounds of LCG:
 *
 * index i     LCG(seed) -> +i   -> LCG -> LCG -> mod 256 -> b[i]
 * index i+1   LCG(seed) -> +i+1 -> LCG -> LCG -> mod 256 -> b[i+1]
 * etc
 *
 * This LCG is the same one as used in glibc.
 */
static inline uint32_t
LCG (uint32_t s)
{
  s *= 1103515245;
  s += 12345;
  return s;
}

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

#define random_config_help \
  "size=<SIZE>  (required) Size of the backing disk\n" \
  "seed=<SEED>             Random number generator seed"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* No meaning, just used as the address for the handle. */
static int rndh;

/* Create the per-connection handle. */
static void *
random_open (int readonly)
{
  return &rndh;
}

/* Get the disk size. */
static int64_t
random_get_size (void *handle)
{
  return (int64_t) size;
}

/* Read data. */
static int
random_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
              uint32_t flags)
{
  size_t i;
  unsigned char *b = buf;
  uint32_t s;

  for (i = 0; i < count; ++i) {
    s = LCG (seed) + offset + i;
    s = LCG (s);
    s = LCG (s);
    s = s % 255;
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
  .open              = random_open,
  .get_size          = random_get_size,
  .pread             = random_pread,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
