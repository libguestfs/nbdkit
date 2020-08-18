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

/* Notes on the design and implementation of this filter:
 *
 * The filter works by creating a large, sparse temporary file, the
 * same size as the underlying device.  Being sparse, initially this
 * takes up no space.
 *
 * We confine all pread/pwrite operations to the filesystem block
 * size.  The blk_* functions below only work on whole filesystem
 * block boundaries.  A smaller-than-block-size pwrite will turn into
 * a read-modify-write of a whole block.  We also assume that the
 * plugin returns the same immutable data for each pread call we make,
 * and optimize on this basis.
 *
 * A block bitmap is maintained in memory recording if each block in
 * the temporary file is "allocated" (1) or "hole" (0).
 *
 * When reading a block we first check the bitmap to see if that file
 * block is allocated or a hole.  If allocated, we return it from the
 * temporary file.  If a hole, we issue a pread to the underlying
 * plugin.
 *
 * When writing a block we unconditionally write the data to the
 * temporary file, setting the bit in the bitmap.
 *
 * We allow the client to request FUA, and emulate it with a flush
 * (arguably, since the write overlay is temporary, we could ignore
 * FUA altogether).
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

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <nbdkit-filter.h>

#include "bitmap.h"
#include "fdatasync.h"
#include "pread.h"
#include "pwrite.h"
#include "utils.h"

#include "blk.h"

/* The temporary overlay. */
static int fd = -1;

/* Bitmap.  Bit = 1 => allocated, 0 => hole. */
static struct bitmap bm;

int
blk_init (void)
{
  const char *tmpdir;
  size_t len;
  char *template;

  bitmap_init (&bm, BLKSIZE, 1 /* bits per block */);

  tmpdir = getenv ("TMPDIR");
  if (!tmpdir)
    tmpdir = LARGE_TMPDIR;

  nbdkit_debug ("cow: temporary directory for overlay: %s", tmpdir);

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
  return 0;
}

void
blk_free (void)
{
  if (fd >= 0)
    close (fd);

  bitmap_free (&bm);
}

/* Allocate or resize the overlay file and bitmap. */
int
blk_set_size (uint64_t new_size)
{
  if (bitmap_resize (&bm, new_size) == -1)
    return -1;

  if (ftruncate (fd, new_size) == -1) {
    nbdkit_error ("ftruncate: %m");
    return -1;
  }

  return 0;
}

/* Return true if the block is allocated.  Consults the bitmap. */
static bool
blk_is_allocated (uint64_t blknum)
{
  return bitmap_get_blk (&bm, blknum, false);
}

/* Mark a block as allocated. */
static void
blk_set_allocated (uint64_t blknum)
{
  bitmap_set_blk (&bm, blknum, true);
}

/* These are the block operations.  They always read or write a single
 * whole block of size ‘blksize’.
 */
int
blk_read (struct nbdkit_next_ops *next_ops, void *nxdata,
          uint64_t blknum, uint8_t *block, int *err)
{
  off_t offset = blknum * BLKSIZE;
  bool allocated = blk_is_allocated (blknum);

  nbdkit_debug ("cow: blk_read block %" PRIu64 " (offset %" PRIu64 ") is %s",
                blknum, (uint64_t) offset,
                !allocated ? "a hole" : "allocated");

  if (!allocated)               /* Read underlying plugin. */
    return next_ops->pread (nxdata, block, BLKSIZE, offset, 0, err);
  else {                        /* Read overlay. */
    if (pread (fd, block, BLKSIZE, offset) == -1) {
      *err = errno;
      nbdkit_error ("pread: %m");
      return -1;
    }
    return 0;
  }
}

int
blk_cache (struct nbdkit_next_ops *next_ops, void *nxdata,
           uint64_t blknum, uint8_t *block, enum cache_mode mode, int *err)
{
  off_t offset = blknum * BLKSIZE;
  bool allocated = blk_is_allocated (blknum);

  nbdkit_debug ("cow: blk_cache block %" PRIu64 " (offset %" PRIu64 ") is %s",
                blknum, (uint64_t) offset,
                !allocated ? "a hole" : "allocated");

  if (allocated) {
#if HAVE_POSIX_FADVISE
    int r = posix_fadvise (fd, offset, BLKSIZE, POSIX_FADV_WILLNEED);
    if (r) {
      errno = r;
      nbdkit_error ("posix_fadvise: %m");
      return -1;
    }
#endif
    return 0;
  }
  if (mode == BLK_CACHE_IGNORE)
    return 0;
  if (mode == BLK_CACHE_PASSTHROUGH)
    return next_ops->cache (nxdata, BLKSIZE, offset, 0, err);
  if (next_ops->pread (nxdata, block, BLKSIZE, offset, 0, err) == -1)
    return -1;
  if (mode == BLK_CACHE_COW) {
    if (pwrite (fd, block, BLKSIZE, offset) == -1) {
      *err = errno;
      nbdkit_error ("pwrite: %m");
      return -1;
    }
    blk_set_allocated (blknum);
  }
  return 0;
}

int
blk_write (uint64_t blknum, const uint8_t *block, int *err)
{
  off_t offset = blknum * BLKSIZE;

  nbdkit_debug ("cow: blk_write block %" PRIu64 " (offset %" PRIu64 ")",
                blknum, (uint64_t) offset);

  if (pwrite (fd, block, BLKSIZE, offset) == -1) {
    *err = errno;
    nbdkit_error ("pwrite: %m");
    return -1;
  }
  blk_set_allocated (blknum);

  return 0;
}

int
blk_flush (void)
{
  /* I think we don't care about file metadata for this temporary
   * file, so only flush the data.
   */
  if (fdatasync (fd) == -1) {
    nbdkit_error ("fdatasync: %m");
    return -1;
  }

  return 0;
}
