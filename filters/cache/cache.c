/* nbdkit
 * Copyright (C) 2018 Red Hat Inc.
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
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <assert.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <nbdkit-filter.h>

#include "rounding.h"

/* XXX See design comment in filters/cow/cow.c. */
#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

/* Size of a block in the cache.  A 4K block size means that we need
 * 64 MB of memory to store the bitmaps for a 1 TB underlying image.
 * It is also smaller than the usual hole size for sparse files, which
 * means we have no reason to call next_ops->zero.
 */
#define BLKSIZE 4096

/* The cache. */
static int fd = -1;

/* Bitmap.  There are two bits per block which are updated as we read,
 * write back or write through blocks.
 *
 * 00 = not in cache
 * 01 = block cached and clean
 * 10 = <unused>
 * 11 = block cached and dirty
 */
static uint8_t *bitmap;

/* Size of the bitmap in bytes. */
static size_t bm_size;

enum bm_entry {
  BLOCK_NOT_CACHED = 0,
  BLOCK_CLEAN = 1,
  BLOCK_DIRTY = 3,
};

/* Caching mode. */
static enum cache_mode {
  CACHE_MODE_WRITEBACK,
  CACHE_MODE_WRITETHROUGH,
  CACHE_MODE_UNSAFE,
} cache_mode = CACHE_MODE_WRITEBACK;

static int
cache_flush (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle,
             uint32_t flags, int *err);

static void
cache_load (void)
{
  const char *tmpdir;
  size_t len;
  char *template;

  tmpdir = getenv ("TMPDIR");
  if (!tmpdir)
    tmpdir = "/var/tmp";

  nbdkit_debug ("cache: temporary directory for cache: %s", tmpdir);

  len = strlen (tmpdir) + 8;
  template = alloca (len);
  snprintf (template, len, "%s/XXXXXX", tmpdir);

#ifdef HAVE_MKOSTEMP
  fd = mkostemp (template, O_CLOEXEC);
#else
  fd = mkstemp (template);
  fcntl (fd, F_SETFD, FD_CLOEXEC);
#endif
  if (fd == -1) {
    nbdkit_error ("mkostemp: %s: %m", tmpdir);
    exit (EXIT_FAILURE);
  }

  unlink (template);
}

static void
cache_unload (void)
{
  if (fd >= 0)
    close (fd);
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
      nbdkit_error ("invalid cache parameter, should be writeback|writethrough|unsafe");
      return -1;
    }
  }
  else {
    return next (nxdata, key, value);
  }
}

static void *
cache_open (nbdkit_next_open *next, void *nxdata, int readonly)
{
  /* We don't use the handle, so this just provides a non-NULL
   * pointer that we can return.
   */
  static int handle;

  if (next (nxdata, readonly) == -1)
    return NULL;

  return &handle;
}

/* Allocate or resize the cache file and bitmap. */
static int
blk_set_size (uint64_t new_size)
{
  uint8_t *new_bm;
  const size_t old_bm_size = bm_size;
  uint64_t new_bm_size_u64 = DIV_ROUND_UP (new_size, BLKSIZE*8/2);
  size_t new_bm_size;

  if (new_bm_size_u64 > SIZE_MAX) {
    nbdkit_error ("bitmap too large for this architecture");
    return -1;
  }
  new_bm_size = (size_t) new_bm_size_u64;

  new_bm = realloc (bitmap, new_bm_size);
  if (new_bm == NULL) {
    nbdkit_error ("realloc: %m");
    return -1;
  }
  bitmap = new_bm;
  bm_size = new_bm_size;
  if (old_bm_size < new_bm_size)
    memset (&bitmap[old_bm_size], 0, new_bm_size-old_bm_size);

  nbdkit_debug ("cache: bitmap resized to %zu bytes", new_bm_size);

  if (ftruncate (fd, new_size) == -1) {
    nbdkit_error ("ftruncate: %m");
    return -1;
  }

  return 0;
}

/* Get the file size and ensure the cache is the correct size. */
static int64_t
cache_get_size (struct nbdkit_next_ops *next_ops, void *nxdata,
              void *handle)
{
  int64_t size;

  size = next_ops->get_size (nxdata);
  if (size == -1)
    return -1;

  nbdkit_debug ("cache: underlying file size: %" PRIi64, size);

  if (blk_set_size (size))
    return -1;

  return size;
}

/* Force an early call to cache_get_size, consequently truncating the
 * cache to the correct size.
 */
static int
cache_prepare (struct nbdkit_next_ops *next_ops, void *nxdata,
               void *handle)
{
  int64_t r;

  r = cache_get_size (next_ops, nxdata, handle);
  if (r < 0)
    return -1;
  /* TODO: cache per-connection FUA mode? */
  return 0;
}

/* Return true if the block is allocated.  Consults the bitmap. */
static enum bm_entry
blk_get_bitmap_entry (uint64_t blknum)
{
  uint64_t bm_offset = blknum / 4;
  uint64_t bm_bit = 2 * (blknum % 4);

  if (bm_offset >= bm_size) {
    nbdkit_debug ("blk_get_bitmap_entry: block number is out of range");
    return BLOCK_NOT_CACHED;
  }

  return (bitmap[bm_offset] & (3 << bm_bit)) >> bm_bit;
}

/* Update cache state of a block. */
static void
blk_set_bitmap_entry (uint64_t blknum, enum bm_entry state)
{
  uint64_t bm_offset = blknum / 4;
  uint64_t bm_bit = 2 * (blknum % 4);

  if (bm_offset >= bm_size) {
    nbdkit_debug ("blk_set_bitmap_entry: block number is out of range");
    return;
  }

  bitmap[bm_offset] |= (unsigned) state << bm_bit;
}

/* These are the block operations.  They always read or write a single
 * whole block of size ‘blksize’.
 */
static int
blk_read (struct nbdkit_next_ops *next_ops, void *nxdata,
          uint64_t blknum, uint8_t *block, int *err)
{
  off_t offset = blknum * BLKSIZE;
  enum bm_entry state = blk_get_bitmap_entry (blknum);

  nbdkit_debug ("cache: blk_read block %" PRIu64 " (offset %" PRIu64 ") is %s",
                blknum, (uint64_t) offset,
                state == BLOCK_NOT_CACHED ? "not cached" :
                state == BLOCK_CLEAN ? "clean" :
                state == BLOCK_DIRTY ? "dirty" :
                "unknown");

  if (state == BLOCK_NOT_CACHED) /* Read underlying plugin. */
    return next_ops->pread (nxdata, block, BLKSIZE, offset, 0, err);
  else {                         /* Read cache. */
    if (pread (fd, block, BLKSIZE, offset) == -1) {
      *err = errno;
      nbdkit_error ("pread: %m");
      return -1;
    }
    return 0;
  }
}

/* Write to the cache and the plugin. */
static int
blk_writethrough (struct nbdkit_next_ops *next_ops, void *nxdata,
                  uint64_t blknum, const uint8_t *block, uint32_t flags,
                  int *err)
{
  off_t offset = blknum * BLKSIZE;

  nbdkit_debug ("cache: blk_writethrough block %" PRIu64
                " (offset %" PRIu64 ")",
                blknum, (uint64_t) offset);

  if (pwrite (fd, block, BLKSIZE, offset) == -1) {
    *err = errno;
    nbdkit_error ("pwrite: %m");
    return -1;
  }

  if (next_ops->pwrite (nxdata, block, BLKSIZE, offset, flags, err) == -1)
    return -1;

  blk_set_bitmap_entry (blknum, BLOCK_CLEAN);

  return 0;
}

/* Write to the cache only. */
static int
blk_writeback (struct nbdkit_next_ops *next_ops, void *nxdata,
               uint64_t blknum, const uint8_t *block, uint32_t flags,
               int *err)
{
  off_t offset;

  if (cache_mode == CACHE_MODE_WRITETHROUGH ||
      (cache_mode == CACHE_MODE_WRITEBACK && (flags & NBDKIT_FLAG_FUA)))
    return blk_writethrough (next_ops, nxdata, blknum, block, flags, err);

  offset = blknum * BLKSIZE;

  nbdkit_debug ("cache: blk_writeback block %" PRIu64
                " (offset %" PRIu64 ")",
                blknum, (uint64_t) offset);

  if (pwrite (fd, block, BLKSIZE, offset) == -1) {
    *err = errno;
    nbdkit_error ("pwrite: %m");
    return -1;
  }
  blk_set_bitmap_entry (blknum, BLOCK_DIRTY);

  return 0;
}

/* Read data. */
static int
cache_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle, void *buf, uint32_t count, uint64_t offset,
             uint32_t flags, int *err)
{
  uint8_t *block;

  assert (!flags);
  block = malloc (BLKSIZE);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  while (count > 0) {
    uint64_t blknum, blkoffs, n;

    blknum = offset / BLKSIZE;  /* block number */
    blkoffs = offset % BLKSIZE; /* offset within the block */
    n = BLKSIZE - blkoffs;      /* max bytes we can read from this block */
    if (n > count)
      n = count;

    if (blk_read (next_ops, nxdata, blknum, block, err) == -1) {
      free (block);
      return -1;
    }

    memcpy (buf, &block[blkoffs], n);

    buf += n;
    count -= n;
    offset += n;
  }

  free (block);
  return 0;
}

/* Write data. */
static int
cache_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
              void *handle, const void *buf, uint32_t count, uint64_t offset,
              uint32_t flags, int *err)
{
  uint8_t *block;
  bool need_flush = false;

  block = malloc (BLKSIZE);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  if ((flags & NBDKIT_FLAG_FUA) &&
      next_ops->can_fua (nxdata) == NBDKIT_FUA_EMULATE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }
  while (count > 0) {
    uint64_t blknum, blkoffs, n;

    blknum = offset / BLKSIZE;  /* block number */
    blkoffs = offset % BLKSIZE; /* offset within the block */
    n = BLKSIZE - blkoffs;      /* max bytes we can read from this block */
    if (n > count)
      n = count;

    /* Do a read-modify-write operation on the current block. */
    if (blk_read (next_ops, nxdata, blknum, block, err) == -1){
      free (block);
      return -1;
    }
    memcpy (&block[blkoffs], buf, n);
    if (blk_writeback (next_ops, nxdata, blknum, block, flags, err) == -1) {
      free (block);
      return -1;
    }

    buf += n;
    count -= n;
    offset += n;
  }

  free (block);
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
  uint8_t *block;
  bool need_flush = false;

  block = malloc (BLKSIZE);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  flags &= ~NBDKIT_FLAG_MAY_TRIM; /* See BLKSIZE comment above. */
  if ((flags & NBDKIT_FLAG_FUA) &&
      next_ops->can_fua (nxdata) == NBDKIT_FUA_EMULATE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }
  while (count > 0) {
    uint64_t blknum, blkoffs, n;

    blknum = offset / BLKSIZE;  /* block number */
    blkoffs = offset % BLKSIZE; /* offset within the block */
    n = BLKSIZE - blkoffs;      /* max bytes we can read from this block */
    if (n > count)
      n = count;

    if (blk_read (next_ops, nxdata, blknum, block, err) == -1) {
      free (block);
      return -1;
    }
    memset (&block[blkoffs], 0, n);
    if (blk_writeback (next_ops, nxdata, blknum, block, flags, err) == -1) {
      free (block);
      return -1;
    }

    count -= n;
    offset += n;
  }

  free (block);
  if (need_flush)
    return cache_flush (next_ops, nxdata, handle, 0, err);
  return 0;
}

/* Flush: Go through all the dirty blocks, flushing them to disk. */
static int
cache_flush (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle,
             uint32_t flags, int *err)
{
  uint8_t *block = NULL;
  uint64_t i, j;
  uint64_t blknum;
  enum bm_entry state;
  unsigned errors = 0;
  int tmp;

  if (cache_mode == CACHE_MODE_UNSAFE)
    return 0;

  /* In theory if cache_mode == CACHE_MODE_WRITETHROUGH then there
   * should be no dirty blocks.  However we go through the cache here
   * to be sure.  Also we still need to issue the flush to the
   * underlying storage.
   */
  assert (!flags);
  for (i = 0; i < bm_size; ++i) {
    if (bitmap[i] != 0) {
      /* The bitmap stores information about 4 blocks per byte,
       * therefore ...
       */
      for (j = 0; j < 4; ++j) {
        blknum = i*4+j;
        state = blk_get_bitmap_entry (blknum);
        if (state == BLOCK_DIRTY) {
          /* Lazily allocate the bounce buffer. */
          if (!block) {
            block = malloc (BLKSIZE);
            if (block == NULL) {
              *err = errno;
              nbdkit_error ("malloc: %m");
              return -1;
            }
          }
          /* Perform a read + writethrough which will read from the
           * cache and write it through to the underlying storage.
           */
          if (blk_read (next_ops, nxdata, blknum, block,
                        errors ? &tmp : err) == -1 ||
              blk_writethrough (next_ops, nxdata, blknum, block, 0,
                                errors ? &tmp : err) == -1) {
            nbdkit_error ("cache: flush of block %" PRIu64 " failed", blknum);
            errors++;
          }
        }
      }
    }
  }

  free (block);

  /* Now issue a flush request to the underlying storage. */
  if (next_ops->flush (nxdata, 0, errors ? &tmp : err) == -1)
    errors++;

  return errors == 0 ? 0 : -1;
}

static struct nbdkit_filter filter = {
  .name              = "cache",
  .longname          = "nbdkit caching filter",
  .version           = PACKAGE_VERSION,
  .load              = cache_load,
  .unload            = cache_unload,
  .config            = cache_config,
  .open              = cache_open,
  .prepare           = cache_prepare,
  .get_size          = cache_get_size,
  .pread             = cache_pread,
  .pwrite            = cache_pwrite,
  .zero              = cache_zero,
  .flush             = cache_flush,
};

NBDKIT_REGISTER_FILTER(filter)
