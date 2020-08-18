/* nbdkit
 * Copyright (C) 2018-2020 Red Hat Inc.
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
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#include <pthread.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <nbdkit-filter.h>

#include "cleanup.h"

#include "cache.h"
#include "blk.h"
#include "reclaim.h"
#include "isaligned.h"
#include "minmax.h"
#include "rounding.h"

/* In order to handle parallel requests safely, this lock must be held
 * when calling any blk_* functions.
 */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

unsigned blksize;
enum cache_mode cache_mode = CACHE_MODE_WRITEBACK;
int64_t max_size = -1;
unsigned hi_thresh = 95, lo_thresh = 80;
bool cache_on_read = false;

static int cache_flush (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle, uint32_t flags, int *err);

static void
cache_load (void)
{
  if (blk_init () == -1)
    exit (EXIT_FAILURE);
}

static void
cache_unload (void)
{
  blk_free ();
}

static int
cache_config (nbdkit_next_config *next, void *nxdata,
              const char *key, const char *value)
{
  if (strcmp (key, "cache") == 0) {
    if (strcmp (value, "writeback") == 0) {
      cache_mode = CACHE_MODE_WRITEBACK;
      return 0;
    }
    else if (strcmp (value, "writethrough") == 0) {
      cache_mode = CACHE_MODE_WRITETHROUGH;
      return 0;
    }
    else if (strcmp (value, "unsafe") == 0) {
      cache_mode = CACHE_MODE_UNSAFE;
      return 0;
    }
    else {
      nbdkit_error ("invalid cache parameter, should be "
                    "writeback|writethrough|unsafe");
      return -1;
    }
  }
#ifdef HAVE_CACHE_RECLAIM
  else if (strcmp (key, "cache-max-size") == 0) {
    int64_t r;

    r = nbdkit_parse_size (value);
    if (r == -1)
      return -1;
    /* We set a lower limit for the cache size just to keep out of
     * trouble.
     */
    if (r < 1024*1024) {
      nbdkit_error ("cache-max-size is too small");
      return -1;
    }
    max_size = r;
    return 0;
  }
  else if (strcmp (key, "cache-high-threshold") == 0) {
    if (nbdkit_parse_unsigned ("cache-high-threshold",
                               value, &hi_thresh) == -1)
      return -1;
    if (hi_thresh == 0) {
      nbdkit_error ("cache-high-threshold must be greater than zero");
      return -1;
    }
    return 0;
  }
  else if (strcmp (key, "cache-low-threshold") == 0) {
    if (nbdkit_parse_unsigned ("cache-low-threshold",
                               value, &lo_thresh) == -1)
      return -1;
    if (lo_thresh == 0) {
      nbdkit_error ("cache-low-threshold must be greater than zero");
      return -1;
    }
    return 0;
  }
#else /* !HAVE_CACHE_RECLAIM */
  else if (strcmp (key, "cache-max-size") == 0 ||
           strcmp (key, "cache-high-threshold") == 0 ||
           strcmp (key, "cache-low-threshold") == 0) {
    nbdkit_error ("this platform does not support cache reclaim");
    return -1;
  }
#endif /* !HAVE_CACHE_RECLAIM */
  else if (strcmp (key, "cache-on-read") == 0) {
    int r;

    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    cache_on_read = r;
    return 0;
  }
  else {
    return next (nxdata, key, value);
  }
}

#define cache_config_help_common \
  "cache=MODE                Set cache MODE, one of writeback (default),\n" \
  "                          writethrough, or unsafe.\n" \
  "cache-on-read=BOOL        Set to true to cache on reads (default false).\n"
#ifndef HAVE_CACHE_RECLAIM
#define cache_config_help cache_config_help_common
#else
#define cache_config_help cache_config_help_common \
  "cache-max-size=SIZE       Set maximum space used by cache.\n" \
  "cache-high-threshold=PCT  Percentage of max size where reclaim begins.\n" \
  "cache-low-threshold=PCT   Percentage of max size where reclaim ends.\n"
#endif

static int
cache_config_complete (nbdkit_next_config_complete *next, void *nxdata)
{
  /* If cache-max-size was set then check the thresholds. */
  if (max_size != -1) {
    if (lo_thresh >= hi_thresh) {
      nbdkit_error ("cache-low-threshold must be "
                    "less than cache-high-threshold");
      return -1;
    }
  }

  return next (nxdata);
}

/* Get the file size; round it down to cache granularity before
 * setting cache size.
 */
static int64_t
cache_get_size (struct nbdkit_next_ops *next_ops, void *nxdata,
                void *handle)
{
  int64_t size;
  int r;

  size = next_ops->get_size (nxdata);
  if (size == -1)
    return -1;

  nbdkit_debug ("cache: underlying file size: %" PRIi64, size);
  size = ROUND_DOWN (size, blksize);

  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
  r = blk_set_size (size);
  if (r == -1)
    return -1;

  return size;
}

/* Force an early call to cache_get_size, consequently truncating the
 * cache to the correct size.
 */
static int
cache_prepare (struct nbdkit_next_ops *next_ops, void *nxdata,
               void *handle, int readonly)
{
  int64_t r;

  r = cache_get_size (next_ops, nxdata, handle);
  if (r < 0)
    return -1;
  return 0;
}

/* Override the plugin's .can_cache, because we are caching here instead */
static int
cache_can_cache (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  return NBDKIT_CACHE_NATIVE;
}

/* Override the plugin's .can_fast_zero, because our .zero is not fast */
static int
cache_can_fast_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
                     void *handle)
{
  /* It is better to advertise support even when we always reject fast
   * zero attempts.
   */
  return 1;
}

/* Read data. */
static int
cache_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle, void *buf, uint32_t count, uint64_t offset,
             uint32_t flags, int *err)
{
  CLEANUP_FREE uint8_t *block = NULL;
  uint64_t blknum, blkoffs;
  int r;

  assert (!flags);
  if (!IS_ALIGNED (count | offset, blksize)) {
    block = malloc (blksize);
    if (block == NULL) {
      *err = errno;
      nbdkit_error ("malloc: %m");
      return -1;
    }
  }

  blknum = offset / blksize;  /* block number */
  blkoffs = offset % blksize; /* offset within the block */

  /* Unaligned head */
  if (blkoffs) {
    uint64_t n = MIN (blksize - blkoffs, count);

    assert (block);
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    r = blk_read (next_ops, nxdata, blknum, block, err);
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
  while (count >= blksize) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    r = blk_read (next_ops, nxdata, blknum, buf, err);
    if (r == -1)
      return -1;

    buf += blksize;
    count -= blksize;
    offset += blksize;
    blknum++;
  }

  /* Unaligned tail */
  if (count) {
    assert (block);
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    r = blk_read (next_ops, nxdata, blknum, block, err);
    if (r == -1)
      return -1;

    memcpy (buf, block, count);
  }

  return 0;
}

/* Write data. */
static int
cache_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
              void *handle, const void *buf, uint32_t count, uint64_t offset,
              uint32_t flags, int *err)
{
  CLEANUP_FREE uint8_t *block = NULL;
  uint64_t blknum, blkoffs;
  int r;
  bool need_flush = false;

  if (!IS_ALIGNED (count | offset, blksize)) {
    block = malloc (blksize);
    if (block == NULL) {
      *err = errno;
      nbdkit_error ("malloc: %m");
      return -1;
    }
  }

  if ((flags & NBDKIT_FLAG_FUA) &&
      next_ops->can_fua (nxdata) == NBDKIT_FUA_EMULATE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }

  blknum = offset / blksize;  /* block number */
  blkoffs = offset % blksize; /* offset within the block */

  /* Unaligned head */
  if (blkoffs) {
    uint64_t n = MIN (blksize - blkoffs, count);

    /* Do a read-modify-write operation on the current block.
     * Hold the lock over the whole operation.
     */
    assert (block);
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    r = blk_read (next_ops, nxdata, blknum, block, err);
    if (r != -1) {
      memcpy (&block[blkoffs], buf, n);
      r = blk_write (next_ops, nxdata, blknum, block, flags, err);
    }
    if (r == -1)
      return -1;

    buf += n;
    count -= n;
    offset += n;
    blknum++;
  }

  /* Aligned body */
  while (count >= blksize) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    r = blk_write (next_ops, nxdata, blknum, buf, flags, err);
    if (r == -1)
      return -1;

    buf += blksize;
    count -= blksize;
    offset += blksize;
    blknum++;
  }

  /* Unaligned tail */
  if (count) {
    assert (block);
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    r = blk_read (next_ops, nxdata, blknum, block, err);
    if (r != -1) {
      memcpy (block, buf, count);
      r = blk_write (next_ops, nxdata, blknum, block, flags, err);
    }
    if (r == -1)
      return -1;
  }

  if (need_flush)
    return cache_flush (next_ops, nxdata, handle, 0, err);
  return 0;
}

/* Zero data. */
static int
cache_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle, uint32_t count, uint64_t offset, uint32_t flags,
            int *err)
{
  CLEANUP_FREE uint8_t *block = NULL;
  uint64_t blknum, blkoffs;
  int r;
  bool need_flush = false;

  /* We are purposefully avoiding next_ops->zero, so a zero request is
   * never faster than plain writes.
   */
  if (flags & NBDKIT_FLAG_FAST_ZERO) {
    *err = ENOTSUP;
    return -1;
  }

  block = malloc (blksize);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  flags &= ~NBDKIT_FLAG_MAY_TRIM;
  if ((flags & NBDKIT_FLAG_FUA) &&
      next_ops->can_fua (nxdata) == NBDKIT_FUA_EMULATE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }

  blknum = offset / blksize;  /* block number */
  blkoffs = offset % blksize; /* offset within the block */

  /* Unaligned head */
  if (blkoffs) {
    uint64_t n = MIN (blksize - blkoffs, count);

    /* Do a read-modify-write operation on the current block.
     * Hold the lock over the whole operation.
     */
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    r = blk_read (next_ops, nxdata, blknum, block, err);
    if (r != -1) {
      memset (&block[blkoffs], 0, n);
      r = blk_write (next_ops, nxdata, blknum, block, flags, err);
    }
    if (r == -1)
      return -1;

    count -= n;
    offset += n;
    blknum++;
  }

  /* Aligned body */
  if (count >= blksize)
    memset (block, 0, blksize);
  while (count >=blksize) {
    /* Intentional that we do not use next_ops->zero */
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    r = blk_write (next_ops, nxdata, blknum, block, flags, err);
    if (r == -1)
      return -1;

    count -= blksize;
    offset += blksize;
    blknum++;
  }

  /* Unaligned tail */
  if (count) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    r = blk_read (next_ops, nxdata, blknum, block, err);
    if (r != -1) {
      memset (&block[count], 0, blksize - count);
      r = blk_write (next_ops, nxdata, blknum, block, flags, err);
    }
    if (r == -1)
      return -1;
  }

  if (need_flush)
    return cache_flush (next_ops, nxdata, handle, 0, err);
  return 0;
}

/* Flush: Go through all the dirty blocks, flushing them to disk. */
struct flush_data {
  uint8_t *block;               /* bounce buffer */
  unsigned errors;              /* count of errors seen */
  int first_errno;              /* first errno seen */
  struct nbdkit_next_ops *next_ops;
  void *nxdata;
};

static int flush_dirty_block (uint64_t blknum, void *);

static int
cache_flush (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle,
             uint32_t flags, int *err)
{
  CLEANUP_FREE uint8_t *block = NULL;
  struct flush_data data =
    { .errors = 0, .first_errno = 0, .next_ops = next_ops, .nxdata = nxdata };
  int tmp;

  if (cache_mode == CACHE_MODE_UNSAFE)
    return 0;

  assert (!flags);

  /* Allocate the bounce buffer. */
  block = malloc (blksize);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }
  data.block = block;

  /* In theory if cache_mode == CACHE_MODE_WRITETHROUGH then there
   * should be no dirty blocks.  However we go through the cache here
   * to be sure.  Also we still need to issue the flush to the
   * underlying storage.
   */
  {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    for_each_dirty_block (flush_dirty_block, &data);
  }

  /* Now issue a flush request to the underlying storage. */
  if (next_ops->flush (nxdata, 0,
                       data.errors ? &tmp : &data.first_errno) == -1)
    data.errors++;

  if (data.errors > 0) {
    *err = data.first_errno;
    return -1;
  }
  return 0;
}

static int
flush_dirty_block (uint64_t blknum, void *datav)
{
  struct flush_data *data = datav;
  int tmp;

  /* Perform a read + writethrough which will read from the
   * cache and write it through to the underlying storage.
   */
  if (blk_read (data->next_ops, data->nxdata, blknum, data->block,
                data->errors ? &tmp : &data->first_errno) == -1)
    goto err;
  if (blk_writethrough (data->next_ops, data->nxdata, blknum, data->block, 0,
                        data->errors ? &tmp : &data->first_errno) == -1)
    goto err;

  return 0;

 err:
  nbdkit_error ("cache: flush of block %" PRIu64 " failed", blknum);
  data->errors++;
  return 0; /* continue scanning and flushing. */
}

/* Cache data. */
static int
cache_cache (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle, uint32_t count, uint64_t offset,
             uint32_t flags, int *err)
{
  CLEANUP_FREE uint8_t *block = NULL;
  uint64_t blknum, blkoffs;
  int r;
  uint64_t remaining = count; /* Rounding out could exceed 32 bits */

  assert (!flags);
  block = malloc (blksize);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  blknum = offset / blksize;  /* block number */
  blkoffs = offset % blksize; /* offset within the block */

  /* Unaligned head */
  remaining += blkoffs;
  offset -= blkoffs;

  /* Unaligned tail */
  remaining = ROUND_UP (remaining, blksize);

  /* Aligned body */
  while (remaining) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    r = blk_cache (next_ops, nxdata, blknum, block, err);
    if (r == -1)
      return -1;

    remaining -= blksize;
    offset += blksize;
    blknum++;
  }

  return 0;
}

static struct nbdkit_filter filter = {
  .name              = "cache",
  .longname          = "nbdkit caching filter",
  .load              = cache_load,
  .unload            = cache_unload,
  .config            = cache_config,
  .config_complete   = cache_config_complete,
  .config_help       = cache_config_help,
  .prepare           = cache_prepare,
  .get_size          = cache_get_size,
  .can_cache         = cache_can_cache,
  .can_fast_zero     = cache_can_fast_zero,
  .pread             = cache_pread,
  .pwrite            = cache_pwrite,
  .zero              = cache_zero,
  .flush             = cache_flush,
  .cache             = cache_cache,
};

NBDKIT_REGISTER_FILTER(filter)
