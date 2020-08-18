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

/* These are the block operations.  They always read or write a single
 * whole block of size ‘blksize’.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifdef HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif

#include <nbdkit-filter.h>

#include "bitmap.h"
#include "minmax.h"
#include "utils.h"

#include "cache.h"
#include "blk.h"
#include "lru.h"
#include "reclaim.h"

/* The cache. */
static int fd = -1;

/* Bitmap.  There are two bits per block which are updated as we read,
 * write back or write through blocks.
 *
 * 00 = not in cache
 * 01 = block cached and clean
 * 10 = <unused>
 * 11 = block cached and dirty
 *
 * Future enhancement:
 *
 * We need to cache information about holes, ie. blocks which read as
 * zeroes but are not explicitly stored in the cache.  This
 * information could be set when clients call cache_zero (and defer
 * calling plugin->zero until flush).  The information could also
 * interact with extents, so when plugin->extents returns information
 * that a hole exists we can record this information in the cache and
 * not have to query the plugin a second time (especially useful for
 * VDDK where querying extents is slow, and for qemu which [in 2019]
 * repeatedly requests the same information with REQ_ONE set).
 */
static struct bitmap bm;

enum bm_entry {
  BLOCK_NOT_CACHED = 0, /* assumed to be zero by reclaim code */
  BLOCK_CLEAN = 1,
  BLOCK_DIRTY = 3,
};

int
blk_init (void)
{
  const char *tmpdir;
  size_t len;
  char *template;
  struct statvfs statvfs;

  tmpdir = getenv ("TMPDIR");
  if (!tmpdir)
    tmpdir = LARGE_TMPDIR;

  nbdkit_debug ("cache: temporary directory for cache: %s", tmpdir);

  len = strlen (tmpdir) + 8;
  template = alloca (len);
  snprintf (template, len, "%s/XXXXXX", tmpdir);

#ifdef HAVE_MKOSTEMP
  fd = mkostemp (template, O_CLOEXEC);
#else
  /* Not atomic, but this is only invoked during .load, so the race
   * won't affect any plugin actions trying to fork
   */
  fd = mkstemp (template);
  if (fd >= 0) {
    fd = set_cloexec (fd);
    if (fd < 0) {
      int e = errno;
      unlink (template);
      errno = e;
    }
  }
#endif
  if (fd == -1) {
    nbdkit_error ("mkostemp: %s: %m", tmpdir);
    return -1;
  }

  unlink (template);

  /* Choose the block size.
   *
   * A 4K block size means that we need 64 MB of memory to store the
   * bitmaps for a 1 TB underlying image.  However to support
   * hole-punching (for reclaiming) we need the block size to be at
   * least as large as the filesystem block size.
   */
  if (fstatvfs (fd, &statvfs) == -1) {
    nbdkit_error ("fstatvfs: %s: %m", tmpdir);
    return -1;
  }
  blksize = MAX (4096, statvfs.f_bsize);
  nbdkit_debug ("cache: block size: %u", blksize);

  bitmap_init (&bm, blksize, 2 /* bits per block */);

  lru_init ();

  return 0;
}

void
blk_free (void)
{
  if (fd >= 0)
    close (fd);

  bitmap_free (&bm);

  lru_free ();
}

int
blk_set_size (uint64_t new_size)
{
  if (bitmap_resize (&bm, new_size) == -1)
    return -1;

  if (ftruncate (fd, new_size) == -1) {
    nbdkit_error ("ftruncate: %m");
    return -1;
  }

  if (lru_set_size (new_size) == -1)
    return -1;

  return 0;
}

int
blk_read (struct nbdkit_next_ops *next_ops, void *nxdata,
          uint64_t blknum, uint8_t *block, int *err)
{
  off_t offset = blknum * blksize;
  enum bm_entry state = bitmap_get_blk (&bm, blknum, BLOCK_NOT_CACHED);

  reclaim (fd, &bm);

  nbdkit_debug ("cache: blk_read block %" PRIu64 " (offset %" PRIu64 ") is %s",
                blknum, (uint64_t) offset,
                state == BLOCK_NOT_CACHED ? "not cached" :
                state == BLOCK_CLEAN ? "clean" :
                state == BLOCK_DIRTY ? "dirty" :
                "unknown");

  if (state == BLOCK_NOT_CACHED) { /* Read underlying plugin. */
    if (next_ops->pread (nxdata, block, blksize, offset, 0, err) == -1)
      return -1;

    /* If cache-on-read, copy the block to the cache. */
    if (cache_on_read) {
      nbdkit_debug ("cache: cache-on-read block %" PRIu64
                    " (offset %" PRIu64 ")",
                    blknum, (uint64_t) offset);

      if (pwrite (fd, block, blksize, offset) == -1) {
        *err = errno;
        nbdkit_error ("pwrite: %m");
        return -1;
      }
      bitmap_set_blk (&bm, blknum, BLOCK_CLEAN);
      lru_set_recently_accessed (blknum);
    }
    return 0;
  }
  else {                        /* Read cache. */
    if (pread (fd, block, blksize, offset) == -1) {
      *err = errno;
      nbdkit_error ("pread: %m");
      return -1;
    }
    lru_set_recently_accessed (blknum);
    return 0;
  }
}

int
blk_cache (struct nbdkit_next_ops *next_ops, void *nxdata,
           uint64_t blknum, uint8_t *block, int *err)
{
  off_t offset = blknum * blksize;
  enum bm_entry state = bitmap_get_blk (&bm, blknum, BLOCK_NOT_CACHED);

  reclaim (fd, &bm);

  nbdkit_debug ("cache: blk_cache block %" PRIu64 " (offset %" PRIu64 ") is %s",
                blknum, (uint64_t) offset,
                state == BLOCK_NOT_CACHED ? "not cached" :
                state == BLOCK_CLEAN ? "clean" :
                state == BLOCK_DIRTY ? "dirty" :
                "unknown");

  if (state == BLOCK_NOT_CACHED) {
    /* Read underlying plugin, copy to cache regardless of cache-on-read. */
    if (next_ops->pread (nxdata, block, blksize, offset, 0, err) == -1)
      return -1;

    nbdkit_debug ("cache: cache block %" PRIu64 " (offset %" PRIu64 ")",
                  blknum, (uint64_t) offset);

    if (pwrite (fd, block, blksize, offset) == -1) {
      *err = errno;
      nbdkit_error ("pwrite: %m");
      return -1;
    }
    bitmap_set_blk (&bm, blknum, BLOCK_CLEAN);
    lru_set_recently_accessed (blknum);
  }
  else {
#if HAVE_POSIX_FADVISE
    int r = posix_fadvise (fd, offset, blksize, POSIX_FADV_WILLNEED);
    if (r) {
      errno = r;
      nbdkit_error ("posix_fadvise: %m");
      return -1;
    }
#endif
    lru_set_recently_accessed (blknum);
  }
  return 0;
}

int
blk_writethrough (struct nbdkit_next_ops *next_ops, void *nxdata,
                  uint64_t blknum, const uint8_t *block, uint32_t flags,
                  int *err)
{
  off_t offset = blknum * blksize;

  reclaim (fd, &bm);

  nbdkit_debug ("cache: writethrough block %" PRIu64 " (offset %" PRIu64 ")",
                blknum, (uint64_t) offset);

  if (pwrite (fd, block, blksize, offset) == -1) {
    *err = errno;
    nbdkit_error ("pwrite: %m");
    return -1;
  }

  if (next_ops->pwrite (nxdata, block, blksize, offset, flags, err) == -1)
    return -1;

  bitmap_set_blk (&bm, blknum, BLOCK_CLEAN);
  lru_set_recently_accessed (blknum);

  return 0;
}

int
blk_write (struct nbdkit_next_ops *next_ops, void *nxdata,
           uint64_t blknum, const uint8_t *block, uint32_t flags,
           int *err)
{
  off_t offset;

  if (cache_mode == CACHE_MODE_WRITETHROUGH ||
      (cache_mode == CACHE_MODE_WRITEBACK && (flags & NBDKIT_FLAG_FUA)))
    return blk_writethrough (next_ops, nxdata, blknum, block, flags, err);

  offset = blknum * blksize;

  reclaim (fd, &bm);

  nbdkit_debug ("cache: writeback block %" PRIu64 " (offset %" PRIu64 ")",
                blknum, (uint64_t) offset);

  if (pwrite (fd, block, blksize, offset) == -1) {
    *err = errno;
    nbdkit_error ("pwrite: %m");
    return -1;
  }
  bitmap_set_blk (&bm, blknum, BLOCK_DIRTY);
  lru_set_recently_accessed (blknum);

  return 0;
}

int
for_each_dirty_block (block_callback f, void *vp)
{
  uint64_t blknum;
  enum bm_entry state;

  bitmap_for (&bm, blknum) {
    state = bitmap_get_blk (&bm, blknum, BLOCK_NOT_CACHED);
    if (state == BLOCK_DIRTY) {
      if (f (blknum, vp) == -1)
        return -1;
    }
  }

  return 0;
}
