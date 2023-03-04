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
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "bitmap.h"
#include "cleanup.h"
#include "isaligned.h"
#include "iszero.h"
#include "minmax.h"
#include "random.h"

static int64_t size = 0;        /* Size of the disk in bytes. */
static uint32_t seed;           /* Random seed. */
static double percent = 10;     /* Percentage of data. */
static uint64_t runlength =     /* Expected average run length of data (bytes)*/
  UINT64_C (16*1024*1024);
static int random_content;      /* false: Repeat same byte  true: Random bytes*/

/* We need to store 1 bit per block.  Using a 4K block size means we
 * need 32M to map each 1T of virtual disk.
 */
#define BLOCKSIZE 4096

static struct bitmap bm;        /* Bitmap of data blocks. */

static void
sparse_random_load (void)
{
  /* Set the seed to a random-ish value.  This is not meant to be
   * cryptographically useful.  It can be overridden using the seed
   * parameter.
   */
  seed = time (NULL);

  bitmap_init (&bm, BLOCKSIZE, 1 /* bits per block */);
}

static void
sparse_random_unload (void)
{
  bitmap_free (&bm);
}

static int
sparse_random_config (const char *key, const char *value)
{
  int64_t r;

  if (strcmp (key, "seed") == 0) {
    if (nbdkit_parse_uint32_t ("seed", value, &seed) == -1)
      return -1;
  }
  else if (strcmp (key, "size") == 0) {
    r = nbdkit_parse_size (value);
    if (r == -1)
      return -1;
    size = r;
  }
  else if (strcmp (key, "percent") == 0) {
    if (sscanf (value, "%lf", &percent) != 1 ||
        percent < 0 || percent > 100) {
      nbdkit_error ("cannot parse percent parameter: %s", value);
      return -1;
    }
  }
  else if (strcmp (key, "runlength") == 0) {
    if (nbdkit_parse_uint64_t ("runlength", value, &runlength) == -1)
      return -1;
    if (runlength <= 0) {
      nbdkit_error ("runlength parameter must be > 0");
      return -1;
    }
  }
  else if (strcmp (key, "random-content") == 0) {
    random_content = nbdkit_parse_bool (value);
    if (random_content == -1)
      return -1;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

#define sparse_random_config_help \
  "size=<SIZE>  (required) Size of the backing disk\n" \
  "seed=<SEED>             Random number generator seed\n" \
  "percent=<PERCENT>       Percentage of data\n" \
  "runlength=<BYTES>       Expected average run length of data\n" \
  "random-content=true     Fully random content in each block"

/* Create the random bitmap of data and holes.
 *
 * We could independently set each block to a random value, but the
 * result wouldn't look much like a virtual machine disk image.
 * Instead we use a strategy which tries to produce runs of data
 * blocks and hole blocks.  We iterate over the blocks keeping track
 * of a current state which is either DATA or HOLE.
 *
 * When in state DATA, we will flip to state HOLE after each block
 * with probability Pᴰᴴ.
 *
 * When in state HOLE, we will flip to state DATA after each block
 * with probability Pᴴᴰ.
 *
 * This will produce runs like this:
 * ◻◻◻◻◻◻◼◼◼◼◻◻◻◻◻◻◻◻◼◼◼◼◼◼◻◻◻◻◻◻◻◻◻◼◼◼◻◻◻◻◻◻◻◻◻◻◻◼◼◼◼◻◻◻◻◻◻◻◻
 * ◼ data block   ◻ hole block
 *
 * By choosing the probabilities Pᴰᴴ and Pᴴᴰ carefully we can target
 * both the desired percentage of data, and the average run length of
 * data blocks, as follows:
 *
 * % data = Pᴴᴰ / (Pᴴᴰ + Pᴰᴴ)
 * average run length = 1 / Pᴰᴴ
 *
 * For example, to achieve the defaults (percent = 10%, runlength =
 * 16M = 4096 blocks), we would use:
 *
 * Pᴰᴴ = 1 / runlength
 *     = 1 / 4096
 *     = 0.000244141
 *
 * Pᴴᴰ = percent * Pᴰᴴ / (1 - percent)
 *     = 0.10 * (1/4096) / (1 - 0.10)
 *     = 0.000027127
 */
static int
sparse_random_get_ready (void)
{
  double P_dh, P_hd, P;
  uint64_t i;
  int state = 0 /* 0 = HOLE, 1 = DATA */;
  struct random_state rs;
  uint64_t nr_data_blocks = 0;
  uint64_t nr_data_runs = 0;
  uint64_t data_run_length = 0;
  uint64_t avg_data_run_length = 0;

  if (bitmap_resize (&bm, size) == -1)
    return -1;

  /* A few special cases first. */
  if (percent == 0)
    return 0;
  if (percent == 100) {
    bitmap_for (&bm, i) {
      bitmap_set_blk (&bm, i, 1);
    }
    return 0;
  }

  /* Otherwise calculate the probability parameters as above. */
  P_dh = 1. / ((double) runlength / BLOCKSIZE);
  P_hd = (percent / 100.) * P_dh / (1. - (percent / 100.));

  nbdkit_debug ("percent requested = %g%%, "
                "expected average run length = %" PRIu64,
                percent, runlength);
  nbdkit_debug ("Pᴰᴴ = %g, Pᴴᴰ = %g", P_dh, P_hd);

  xsrandom (seed, &rs);

  bitmap_for (&bm, i) {
    if (state) bitmap_set_blk (&bm, i, state);

    /* The probability of exiting this state.  If we're in data
     * (state != 0) then it's Pᴰᴴ (data->hole), otherwise it's Pᴴᴰ
     * (hole->data).
     */
    P = state ? P_dh : P_hd;
    if (xrandom (&rs) <= P * (double) UINT64_MAX)
      state ^= 1;
  }

  /* This code is simply for calculating how well we did compared to
   * the target.
   */
  bitmap_for (&bm, i) {
    state = bitmap_get_blk (&bm, i, 0);
    if (state == 1) {
      nr_data_blocks++;
      if (i == 0 || bitmap_get_blk (&bm, i-1, 0) == 0) {
        /* Start of new data run. */
        avg_data_run_length += data_run_length;
        nr_data_runs++;
        data_run_length = 1;
      }
      else
        data_run_length++;
    }
  }
  avg_data_run_length += data_run_length;
  if (nr_data_runs > 0)
    avg_data_run_length /= nr_data_runs;
  else
    avg_data_run_length = 0;
  nbdkit_debug ("percent actual = %g%%, "
                "average run length = %" PRIu64,
                100. * BLOCKSIZE * nr_data_blocks / size,
                avg_data_run_length * BLOCKSIZE);

  return 0;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Create the per-connection handle. */
static void *
sparse_random_open (int readonly)
{
  return NBDKIT_HANDLE_NOT_NEEDED;
}

/* Get the disk size. */
static int64_t
sparse_random_get_size (void *handle)
{
  return size;
}

/* Serves the same data over multiple connections. */
static int
sparse_random_can_multi_conn (void *handle)
{
  return 1;
}

/* Cache. */
static int
sparse_random_can_cache (void *handle)
{
  /* Everything is already in memory, returning this without
   * implementing .cache lets nbdkit do the correct no-op.
   */
  return NBDKIT_CACHE_NATIVE;
}

static void
read_block (uint64_t blknum, uint64_t offset, void *buf)
{
  unsigned char *b = buf;
  uint64_t s;
  uint32_t i;
  struct random_state state;

  if (bitmap_get_blk (&bm, blknum, 0) == 0) /* hole */
    memset (buf, 0, BLOCKSIZE);
  else if (!random_content) {   /* data when random-content=false */
    xsrandom (seed + offset, &state);
    s = xrandom (&state);
    s &= 255;
    if (s == 0) s = 1;
    memset (buf, (int)s, BLOCKSIZE);
  }
  else {                        /* data when random-content=true */
    /* This produces repeatable data for the same offset.  Note it
     * works because we are called on whole blocks only.
     */
    xsrandom (seed + offset, &state);
    for (i = 0; i < BLOCKSIZE; ++i) {
      s = xrandom (&state);
      s &= 255;
      b[i] = s;
    }
  }
}

/* Read data. */
static int
sparse_random_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
                     uint32_t flags)
{
  CLEANUP_FREE uint8_t *block = NULL;
  uint64_t blknum, blkoffs;

  if (!IS_ALIGNED (count | offset, BLOCKSIZE)) {
    block = malloc (BLOCKSIZE);
    if (block == NULL) {
      nbdkit_error ("malloc: %m");
      return -1;
    }
  }

  blknum = offset / BLOCKSIZE;  /* block number */
  blkoffs = offset % BLOCKSIZE; /* offset within the block */

  /* Unaligned head */
  if (blkoffs) {
    uint64_t n = MIN (BLOCKSIZE - blkoffs, count);

    read_block (blknum, offset, block);
    memcpy (buf, &block[blkoffs], n);

    buf += n;
    count -= n;
    offset += n;
    blknum++;
  }

  /* Aligned body */
  while (count >= BLOCKSIZE) {
    read_block (blknum, offset, buf);

    buf += BLOCKSIZE;
    count -= BLOCKSIZE;
    offset += BLOCKSIZE;
    blknum++;
  }

  /* Unaligned tail */
  if (count) {
    read_block (blknum, offset, block);
    memcpy (buf, block, count);
  }

  return 0;
}

/* Write data.
 *
 * This actually checks that what you're writing exactly matches
 * what is expected.
 */
static int
sparse_random_pwrite (void *handle, const void *buf,
                      uint32_t count, uint64_t offset,
                      uint32_t flags)
{
  CLEANUP_FREE uint8_t *block;
  uint64_t blknum, blkoffs;

  block = malloc (BLOCKSIZE);
  if (block == NULL) {
    nbdkit_error ("malloc: %m");
    return -1;
  }

  blknum = offset / BLOCKSIZE;  /* block number */
  blkoffs = offset % BLOCKSIZE; /* offset within the block */

  /* Unaligned head */
  if (blkoffs) {
    uint64_t n = MIN (BLOCKSIZE - blkoffs, count);

    read_block (blknum, offset, block);
    if (memcmp (buf, &block[blkoffs], n) != 0) {
    unexpected_data:
      errno = EIO;
      nbdkit_error ("data written does not match expected");
      return -1;
    }

    buf += n;
    count -= n;
    offset += n;
    blknum++;
  }

  /* Aligned body */
  while (count >= BLOCKSIZE) {
    /* As an optimization, skip calling read_block if we know this is
     * a hole.  Call is_zero instead which should be faster.
     */
    if (bitmap_get_blk (&bm, blknum, 0) == 0) {
      if (! is_zero (buf, BLOCKSIZE))
        goto unexpected_data;
    }
    else {
      read_block (blknum, offset, block);
      if (memcmp (buf, block, BLOCKSIZE) != 0)
        goto unexpected_data;
    }

    buf += BLOCKSIZE;
    count -= BLOCKSIZE;
    offset += BLOCKSIZE;
    blknum++;
  }

  /* Unaligned tail */
  if (count) {
    read_block (blknum, offset, block);
    if (memcmp (buf, block, count) != 0)
      goto unexpected_data;
  }

  return 0;
}

/* Flush.
 *
 * This is required in order to support the nbdcopy --flush option,
 * but it's a no-op since this plugin does not store data.
 */
static int
sparse_random_flush (void *handle, uint32_t flags)
{
  return 0;
}

/* Trim and zero.
 *
 * These only let you "write" to holes.
 */
static int
sparse_random_trim_zero (void *handle, uint32_t count, uint64_t offset,
                         uint32_t flags)
{
  uint64_t blknum, blkoffs;

  blknum = offset / BLOCKSIZE;  /* block number */
  blkoffs = offset % BLOCKSIZE; /* offset within the block */

  /* Unaligned head */
  if (blkoffs) {
    uint64_t n = MIN (BLOCKSIZE - blkoffs, count);

    if (bitmap_get_blk (&bm, blknum, 0) != 0) {
    unexpected_trim:
      errno = EIO;
      nbdkit_error ("trying to trim or zero non-hole in disk");
      return -1;
    }

    count -= n;
    offset += n;
    blknum++;
  }

  /* Aligned body */
  while (count >= BLOCKSIZE) {
    if (bitmap_get_blk (&bm, blknum, 0) != 0)
      goto unexpected_trim;

    count -= BLOCKSIZE;
    offset += BLOCKSIZE;
    blknum++;
  }

  /* Unaligned tail */
  if (count) {
    if (bitmap_get_blk (&bm, blknum, 0) != 0)
      goto unexpected_trim;
  }

  return 0;
}

static int
sparse_random_extents (void *handle, uint32_t count, uint64_t offset,
                       uint32_t flags, struct nbdkit_extents *extents)
{
  uint64_t blknum, blkoffs;
  uint32_t type;

  blknum = offset / BLOCKSIZE;  /* block number */
  blkoffs = offset % BLOCKSIZE; /* offset within the block */

  /* Unaligned head */
  if (blkoffs) {
    uint64_t n = MIN (BLOCKSIZE - blkoffs, count);

    if (bitmap_get_blk (&bm, blknum, 0) == 0)
      type = NBDKIT_EXTENT_HOLE | NBDKIT_EXTENT_ZERO;
    else
      type = 0; /* data */
    if (nbdkit_add_extent (extents, offset, n, type) == -1)
      return -1;

    count -= n;
    offset += n;
    blknum++;
  }

  /* Aligned body */
  while (count >= BLOCKSIZE) {
    if (bitmap_get_blk (&bm, blknum, 0) == 0)
      type = NBDKIT_EXTENT_HOLE | NBDKIT_EXTENT_ZERO;
    else
      type = 0; /* data */
    if (nbdkit_add_extent (extents, offset, BLOCKSIZE, type) == -1)
      return -1;

    count -= BLOCKSIZE;
    offset += BLOCKSIZE;
    blknum++;
  }

  /* Unaligned tail */
  if (count) {
    if (bitmap_get_blk (&bm, blknum, 0) == 0)
      type = NBDKIT_EXTENT_HOLE | NBDKIT_EXTENT_ZERO;
    else
      type = 0; /* data */
    if (nbdkit_add_extent (extents, offset, count, type) == -1)
      return -1;
  }

  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "sparse-random",
  .version           = PACKAGE_VERSION,
  .load              = sparse_random_load,
  .unload            = sparse_random_unload,
  .config            = sparse_random_config,
  .config_help       = sparse_random_config_help,
  .get_ready         = sparse_random_get_ready,
  .magic_config_key  = "size",
  .open              = sparse_random_open,
  .get_size          = sparse_random_get_size,
  .can_multi_conn    = sparse_random_can_multi_conn,
  .can_cache         = sparse_random_can_cache,
  .pread             = sparse_random_pread,
  .pwrite            = sparse_random_pwrite,
  .flush             = sparse_random_flush,
  .trim              = sparse_random_trim_zero,
  .zero              = sparse_random_trim_zero,
  .extents           = sparse_random_extents,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN (plugin)
