/* nbdkit
 * Copyright (C) 2018-2021 Red Hat Inc.
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
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#include <pthread.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "isaligned.h"
#include "minmax.h"
#include "rounding.h"

#include "blk.h"

/* Read-modify-write requests are serialized through this global lock.
 * This is only used for unaligned requests which should be
 * infrequent.
 */
static pthread_mutex_t rmw_lock = PTHREAD_MUTEX_INITIALIZER;

bool cow_on_cache;

static void
cow_load (void)
{
  if (blk_init () == -1)
    exit (EXIT_FAILURE);
}

static void
cow_unload (void)
{
  blk_free ();
}

static int
cow_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
            const char *key, const char *value)
{
  if (strcmp (key, "cow-on-cache") == 0) {
    int r;

    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    cow_on_cache = r;
    return 0;
  }
  else {
    return next (nxdata, key, value);
  }
}

#define cow_config_help \
  "cow-on-cache=<BOOL>  Set to true to treat client cache requests as writes.\n"

static void *
cow_open (nbdkit_next_open *next, nbdkit_context *nxdata,
          int readonly, const char *exportname, int is_tls)
{
  /* Always pass readonly=1 to the underlying plugin. */
  if (next (nxdata, 1, exportname) == -1)
    return NULL;

  return NBDKIT_HANDLE_NOT_NEEDED;
}

/* Get the file size, set the cache size. */
static int64_t
cow_get_size (nbdkit_next *next,
              void *handle)
{
  int64_t size;
  int r;

  size = next->get_size (next);
  if (size == -1)
    return -1;

  nbdkit_debug ("cow: underlying file size: %" PRIi64, size);

  r = blk_set_size (size);
  if (r == -1)
    return -1;

  return size;
}

/* Force an early call to cow_get_size because we have to set the
 * backing file size and bitmap size before any other read or write
 * calls.
 */
static int
cow_prepare (nbdkit_next *next,
             void *handle, int readonly)
{
  int64_t r;

  r = cow_get_size (next, handle);
  return r >= 0 ? 0 : -1;
}

static int
cow_can_write (nbdkit_next *next, void *handle)
{
  return 1;
}

static int
cow_can_trim (nbdkit_next *next, void *handle)
{
  return 1;
}

static int
cow_can_extents (nbdkit_next *next, void *handle)
{
  return 1;
}

static int
cow_can_flush (nbdkit_next *next, void *handle)
{
  return 1;
}

static int
cow_can_fua (nbdkit_next *next, void *handle)
{
  return NBDKIT_FUA_NATIVE;
}

static int
cow_can_cache (nbdkit_next *next, void *handle)
{
  /* Cache next->can_cache now, so later calls to next->cache
   * don't fail, even though we override the answer here.
   */
  int r = next->can_cache (next);
  if (r == -1)
    return -1;
  return NBDKIT_CACHE_NATIVE;
}

static int
cow_can_multi_conn (nbdkit_next *next,
                    void *handle)
{
  /* Our cache is consistent between connections.  */
  return 1;
}

/* Override the plugin's .can_fast_zero, because our .zero is not fast */
static int
cow_can_fast_zero (nbdkit_next *next,
                   void *handle)
{
  /* It is better to advertise support even when we always reject fast
   * zero attempts.
   */
  return 1;
}

static int cow_flush (nbdkit_next *next, void *handle, uint32_t flags,
                      int *err);

/* Read data. */
static int
cow_pread (nbdkit_next *next,
           void *handle, void *buf, uint32_t count, uint64_t offset,
           uint32_t flags, int *err)
{
  CLEANUP_FREE uint8_t *block = NULL;
  uint64_t blknum, blkoffs;
  int r;

  if (!IS_ALIGNED (count | offset, BLKSIZE)) {
    block = malloc (BLKSIZE);
    if (block == NULL) {
      *err = errno;
      nbdkit_error ("malloc: %m");
      return -1;
    }
  }

  blknum = offset / BLKSIZE;  /* block number */
  blkoffs = offset % BLKSIZE; /* offset within the block */

  /* Unaligned head */
  if (blkoffs) {
    uint64_t n = MIN (BLKSIZE - blkoffs, count);

    assert (block);
    r = blk_read (next, blknum, block, err);
    if (r == -1)
      return -1;

    memcpy (buf, &block[blkoffs], n);

    buf += n;
    count -= n;
    offset += n;
    blknum++;
  }

  /* Aligned body */
  /* XXX This breaks up large read requests into smaller ones, which
   * is a problem for plugins which have a large, fixed per-request
   * overhead (hello, curl).  We should try to keep large requests
   * together as much as possible, but that requires us to be much
   * smarter here.
   */
  while (count >= BLKSIZE) {
    r = blk_read (next, blknum, buf, err);
    if (r == -1)
      return -1;

    buf += BLKSIZE;
    count -= BLKSIZE;
    offset += BLKSIZE;
    blknum++;
  }

  /* Unaligned tail */
  if (count) {
    assert (block);
    r = blk_read (next, blknum, block, err);
    if (r == -1)
      return -1;

    memcpy (buf, block, count);
  }

  return 0;
}

/* Write data. */
static int
cow_pwrite (nbdkit_next *next,
            void *handle, const void *buf, uint32_t count, uint64_t offset,
            uint32_t flags, int *err)
{
  CLEANUP_FREE uint8_t *block = NULL;
  uint64_t blknum, blkoffs;
  int r;

  if (!IS_ALIGNED (count | offset, BLKSIZE)) {
    block = malloc (BLKSIZE);
    if (block == NULL) {
      *err = errno;
      nbdkit_error ("malloc: %m");
      return -1;
    }
  }

  blknum = offset / BLKSIZE;  /* block number */
  blkoffs = offset % BLKSIZE; /* offset within the block */

  /* Unaligned head */
  if (blkoffs) {
    uint64_t n = MIN (BLKSIZE - blkoffs, count);

    /* Do a read-modify-write operation on the current block.
     * Hold the rmw_lock over the whole operation.
     */
    assert (block);
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&rmw_lock);
    r = blk_read (next, blknum, block, err);
    if (r != -1) {
      memcpy (&block[blkoffs], buf, n);
      r = blk_write (blknum, block, err);
    }
    if (r == -1)
      return -1;

    buf += n;
    count -= n;
    offset += n;
    blknum++;
  }

  /* Aligned body */
  while (count >= BLKSIZE) {
    r = blk_write (blknum, buf, err);
    if (r == -1)
      return -1;

    buf += BLKSIZE;
    count -= BLKSIZE;
    offset += BLKSIZE;
    blknum++;
  }

  /* Unaligned tail */
  if (count) {
    assert (block);
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&rmw_lock);
    r = blk_read (next, blknum, block, err);
    if (r != -1) {
      memcpy (block, buf, count);
      r = blk_write (blknum, block, err);
    }
    if (r == -1)
      return -1;
  }

  /* flags & NBDKIT_FLAG_FUA is deliberately ignored. */

  return 0;
}

/* Zero data. */
static int
cow_zero (nbdkit_next *next,
          void *handle, uint32_t count, uint64_t offset, uint32_t flags,
          int *err)
{
  CLEANUP_FREE uint8_t *block = NULL;
  uint64_t blknum, blkoffs;
  int r;

  /* We are purposefully avoiding next->zero, so a zero request is
   * never faster than plain writes.
   */
  if (flags & NBDKIT_FLAG_FAST_ZERO) {
    *err = ENOTSUP;
    return -1;
  }

  block = malloc (BLKSIZE);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  blknum = offset / BLKSIZE;  /* block number */
  blkoffs = offset % BLKSIZE; /* offset within the block */

  /* Unaligned head */
  if (blkoffs) {
    uint64_t n = MIN (BLKSIZE - blkoffs, count);

    /* Do a read-modify-write operation on the current block.
     * Hold the rmw_lock over the whole operation.
     */
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&rmw_lock);
    r = blk_read (next, blknum, block, err);
    if (r != -1) {
      memset (&block[blkoffs], 0, n);
      r = blk_write (blknum, block, err);
    }
    if (r == -1)
      return -1;

    count -= n;
    offset += n;
    blknum++;
  }

  /* Aligned body */
  if (count >= BLKSIZE)
    memset (block, 0, BLKSIZE);
  while (count >= BLKSIZE) {
    /* XXX There is the possibility of optimizing this: since this loop is
     * writing a whole, aligned block, we should use FALLOC_FL_ZERO_RANGE.
     */
    r = blk_write (blknum, block, err);
    if (r == -1)
      return -1;

    count -= BLKSIZE;
    offset += BLKSIZE;
    blknum++;
  }

  /* Unaligned tail */
  if (count) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&rmw_lock);
    r = blk_read (next, blknum, block, err);
    if (r != -1) {
      memset (&block[count], 0, BLKSIZE - count);
      r = blk_write (blknum, block, err);
    }
    if (r == -1)
      return -1;
  }

  /* flags & NBDKIT_FLAG_FUA is deliberately ignored. */

  return 0;
}

/* Trim data. */
static int
cow_trim (nbdkit_next *next,
          void *handle, uint32_t count, uint64_t offset, uint32_t flags,
          int *err)
{
  CLEANUP_FREE uint8_t *block = NULL;
  uint64_t blknum, blkoffs;
  int r;

  if (!IS_ALIGNED (count | offset, BLKSIZE)) {
    block = malloc (BLKSIZE);
    if (block == NULL) {
      *err = errno;
      nbdkit_error ("malloc: %m");
      return -1;
    }
  }

  blknum = offset / BLKSIZE;  /* block number */
  blkoffs = offset % BLKSIZE; /* offset within the block */

  /* Unaligned head */
  if (blkoffs) {
    uint64_t n = MIN (BLKSIZE - blkoffs, count);

    /* Do a read-modify-write operation on the current block.
     * Hold the lock over the whole operation.
     */
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&rmw_lock);
    r = blk_read (next, blknum, block, err);
    if (r != -1) {
      memset (&block[blkoffs], 0, n);
      r = blk_write (blknum, block, err);
    }
    if (r == -1)
      return -1;

    count -= n;
    offset += n;
    blknum++;
  }

  /* Aligned body */
  while (count >= BLKSIZE) {
    r = blk_trim (blknum, err);
    if (r == -1)
      return -1;

    count -= BLKSIZE;
    offset += BLKSIZE;
    blknum++;
  }

  /* Unaligned tail */
  if (count) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&rmw_lock);
    r = blk_read (next, blknum, block, err);
    if (r != -1) {
      memset (&block[count], 0, BLKSIZE - count);
      r = blk_write (blknum, block, err);
    }
    if (r == -1)
      return -1;
  }

  /* flags & NBDKIT_FLAG_FUA is deliberately ignored. */

  return 0;
}

static int
cow_flush (nbdkit_next *next, void *handle,
           uint32_t flags, int *err)
{
  /* Deliberately ignored. */
  return 0;
}

static int
cow_cache (nbdkit_next *next,
           void *handle, uint32_t count, uint64_t offset,
           uint32_t flags, int *err)
{
  CLEANUP_FREE uint8_t *block = NULL;
  uint64_t blknum, blkoffs;
  int r;
  uint64_t remaining = count; /* Rounding out could exceed 32 bits */
  enum cache_mode mode;

  switch (next->can_cache (next)) {
  case NBDKIT_CACHE_NONE:
    mode = BLK_CACHE_IGNORE;
    break;
  case NBDKIT_CACHE_EMULATE:
    mode = BLK_CACHE_READ;
    break;
  case NBDKIT_CACHE_NATIVE:
    mode = BLK_CACHE_PASSTHROUGH;
    break;
  default:
    assert (false); /* Guaranteed thanks to early caching */
  }
  if (cow_on_cache)
    mode = BLK_CACHE_COW;

  assert (!flags);
  block = malloc (BLKSIZE);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  blknum = offset / BLKSIZE;  /* block number */
  blkoffs = offset % BLKSIZE; /* offset within the block */

  /* Unaligned head */
  remaining += blkoffs;
  offset -= blkoffs;

  /* Unaligned tail */
  remaining = ROUND_UP (remaining, BLKSIZE);

  /* Aligned body */
  while (remaining) {
    r = blk_cache (next, blknum, block, mode, err);
    if (r == -1)
      return -1;

    remaining -= BLKSIZE;
    offset += BLKSIZE;
    blknum++;
  }

  return 0;
}

/* Extents. */
static int
cow_extents (nbdkit_next *next,
             void *handle, uint32_t count, uint64_t offset, uint32_t flags,
             struct nbdkit_extents *extents, int *err)
{
  const bool can_extents = next->can_extents (next);
  const bool req_one = flags & NBDKIT_FLAG_REQ_ONE;
  uint64_t end;
  uint64_t blknum;

  /* To make this easier, align the requested extents to whole blocks. */
  end = offset + count;
  offset = ROUND_DOWN (offset, BLKSIZE);
  end = ROUND_UP (end, BLKSIZE);
  count  = end - offset;
  blknum = offset / BLKSIZE;

  assert (IS_ALIGNED (offset, BLKSIZE));
  assert (IS_ALIGNED (count, BLKSIZE));
  assert (count > 0);           /* We must make forward progress. */

  while (count > 0) {
    bool present, trimmed;
    struct nbdkit_extent e;

    blk_status (blknum, &present, &trimmed);

    /* Present in the overlay. */
    if (present) {
      e.offset = offset;
      e.length = BLKSIZE;

      if (trimmed)
        e.type = NBDKIT_EXTENT_HOLE|NBDKIT_EXTENT_ZERO;
      else
        e.type = 0;

      if (nbdkit_add_extent (extents, e.offset, e.length, e.type) == -1) {
        *err = errno;
        return -1;
      }

      blknum++;
      offset += BLKSIZE;
      count -= BLKSIZE;
    }

    /* Not present in the overlay, but we can ask the plugin. */
    else if (can_extents) {
      uint64_t range_offset = offset;
      uint32_t range_count = 0;
      size_t i;
      int64_t size;

      /* Asking the plugin for a single block of extents is not
       * efficient for some plugins (eg. VDDK) so ask for as much data
       * as we can.
       */
      for (;;) {
        blknum++;
        offset += BLKSIZE;
        count -= BLKSIZE;
        range_count += BLKSIZE;

        if (count == 0) break;
        blk_status (blknum, &present, &trimmed);
        if (present) break;
      }

      /* Don't ask for extent data beyond the end of the plugin. */
      size = next->get_size (next);
      if (size == -1)
        return -1;

      if (range_offset + range_count > size) {
        unsigned tail = range_offset + range_count - size;
        range_count -= tail;
      }

      CLEANUP_EXTENTS_FREE struct nbdkit_extents *extents2 =
        nbdkit_extents_full (next, range_count, range_offset, flags, err);
      if (extents2 == NULL)
        return -1;

      for (i = 0; i < nbdkit_extents_count (extents2); ++i) {
        e = nbdkit_get_extent (extents2, i);
        if (nbdkit_add_extent (extents, e.offset, e.length, e.type) == -1) {
          *err = errno;
          return -1;
        }
      }
    }

    /* Otherwise assume the block is non-sparse. */
    else {
      e.offset = offset;
      e.length = BLKSIZE;
      e.type = 0;

      if (nbdkit_add_extent (extents, e.offset, e.length, e.type) == -1) {
        *err = errno;
        return -1;
      }

      blknum++;
      offset += BLKSIZE;
      count -= BLKSIZE;
    }

    /* If the caller only wanted the first extent, and we've managed
     * to add at least one extent to the list, then we can drop out
     * now.  (Note calling nbdkit_add_extent above does not mean the
     * extent got added since it might be before the first offset.)
     */
    if (req_one && nbdkit_extents_count (extents) > 0)
      break;
  }

  return 0;
}

static struct nbdkit_filter filter = {
  .name              = "cow",
  .longname          = "nbdkit copy-on-write (COW) filter",
  .load              = cow_load,
  .unload            = cow_unload,
  .open              = cow_open,
  .config            = cow_config,
  .config_help       = cow_config_help,
  .prepare           = cow_prepare,
  .get_size          = cow_get_size,
  .can_write         = cow_can_write,
  .can_flush         = cow_can_flush,
  .can_trim          = cow_can_trim,
  .can_extents       = cow_can_extents,
  .can_fua           = cow_can_fua,
  .can_cache         = cow_can_cache,
  .can_fast_zero     = cow_can_fast_zero,
  .can_multi_conn    = cow_can_multi_conn,
  .pread             = cow_pread,
  .pwrite            = cow_pwrite,
  .zero              = cow_zero,
  .trim              = cow_trim,
  .flush             = cow_flush,
  .cache             = cow_cache,
  .extents           = cow_extents,
};

NBDKIT_REGISTER_FILTER(filter)
