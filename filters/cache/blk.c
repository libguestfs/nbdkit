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

#include <nbdkit-filter.h>

#include "bitmap.h"

#include "cache.h"
#include "blk.h"
#include "lru.h"

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
static struct bitmap bm;

enum bm_entry {
  BLOCK_NOT_CACHED = 0,
  BLOCK_CLEAN = 1,
  BLOCK_DIRTY = 3,
};

int
blk_init (void)
{
  const char *tmpdir;
  size_t len;
  char *template;

  lru_init ();

  bitmap_init (&bm, BLKSIZE, 2 /* bits per block */);

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
  fd = mkstemp (template);
  fcntl (fd, F_SETFD, FD_CLOEXEC);
#endif
  if (fd == -1) {
    nbdkit_error ("mkostemp: %s: %m", tmpdir);
    return -1;
  }

  unlink (template);

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
  off_t offset = blknum * BLKSIZE;
  enum bm_entry state = bitmap_get_blk (&bm, blknum, BLOCK_NOT_CACHED);

  nbdkit_debug ("cache: blk_read block %" PRIu64 " (offset %" PRIu64 ") is %s",
                blknum, (uint64_t) offset,
                state == BLOCK_NOT_CACHED ? "not cached" :
                state == BLOCK_CLEAN ? "clean" :
                state == BLOCK_DIRTY ? "dirty" :
                "unknown");

  if (state == BLOCK_NOT_CACHED) { /* Read underlying plugin. */
    if (next_ops->pread (nxdata, block, BLKSIZE, offset, 0, err) == -1)
      return -1;

    /* If cache-on-read, copy the block to the cache. */
    if (cache_on_read) {
      off_t offset = blknum * BLKSIZE;

      nbdkit_debug ("cache: cache-on-read block %" PRIu64
                    " (offset %" PRIu64 ")",
                    blknum, (uint64_t) offset);

      if (pwrite (fd, block, BLKSIZE, offset) == -1) {
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
    if (pread (fd, block, BLKSIZE, offset) == -1) {
      *err = errno;
      nbdkit_error ("pread: %m");
      return -1;
    }
    lru_set_recently_accessed (blknum);
    return 0;
  }
}

int
blk_writethrough (struct nbdkit_next_ops *next_ops, void *nxdata,
                  uint64_t blknum, const uint8_t *block, uint32_t flags,
                  int *err)
{
  off_t offset = blknum * BLKSIZE;

  nbdkit_debug ("cache: writethrough block %" PRIu64 " (offset %" PRIu64 ")",
                blknum, (uint64_t) offset);

  if (pwrite (fd, block, BLKSIZE, offset) == -1) {
    *err = errno;
    nbdkit_error ("pwrite: %m");
    return -1;
  }

  if (next_ops->pwrite (nxdata, block, BLKSIZE, offset, flags, err) == -1)
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

  offset = blknum * BLKSIZE;

  nbdkit_debug ("cache: writeback block %" PRIu64 " (offset %" PRIu64 ")",
                blknum, (uint64_t) offset);

  if (pwrite (fd, block, BLKSIZE, offset) == -1) {
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
