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
 * No locking is needed for blk_* calls, but there is a potential
 * problem of multiple pwrite calls doing a read-modify-write cycle
 * because the last write would win, erasing earlier writes.  To avoid
 * this we limit the thread model to SERIALIZE_ALL_REQUESTS so that
 * there cannot be concurrent pwrite requests.  We could relax this
 * restriction with a bit of work.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <alloca.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <nbdkit-filter.h>

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

/* XXX See design comment above. */
#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

/* Size of a block in the overlay.  A 4K block size means that we need
 * 32 MB of memory to store the bitmap for a 1 TB underlying image.
 */
#define BLKSIZE 4096

/* The temporary overlay. */
static int fd = -1;

/* Bitmap.  Bit 1 = allocated, 0 = hole. */
static uint8_t *bitmap;

/* Size of the bitmap in bytes. */
static size_t bm_size;

static void
cow_load (void)
{
  const char *tmpdir;
  size_t len;
  char *template;

  tmpdir = getenv ("TMPDIR");
  if (!tmpdir)
    tmpdir = "/var/tmp";

  nbdkit_debug ("cow: temporary directory for overlay: %s", tmpdir);

  len = strlen (tmpdir) + 8;
  template = alloca (len);
  snprintf (template, len, "%s/XXXXXX", tmpdir);

  fd = mkostemp (template, O_CLOEXEC);
  if (fd == -1) {
    nbdkit_error ("mkostemp: %s: %m", tmpdir);
    exit (EXIT_FAILURE);
  }

  unlink (template);
}

static void
cow_unload (void)
{
  if (fd >= 0)
    close (fd);
}

static void *
cow_open (nbdkit_next_open *next, void *nxdata, int readonly)
{
  /* We don't use the handle, so this just provides a non-NULL
   * pointer that we can return.
   */
  static int handle;

  /* Always pass readonly=1 to the underlying plugin. */
  if (next (nxdata, 1) == -1)
    return NULL;

  return &handle;
}

/* Allocate or resize the overlay file and bitmap. */
static int
blk_set_size (uint64_t new_size)
{
  uint8_t *new_bm;
  const size_t old_bm_size = bm_size;
  uint64_t new_bm_size_u64 = DIV_ROUND_UP (new_size, BLKSIZE*8);
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

  nbdkit_debug ("cow: bitmap resized to %zu bytes", new_bm_size);

  if (ftruncate (fd, new_size) == -1) {
    nbdkit_error ("ftruncate: %m");
    return -1;
  }

  return 0;
}

/* Get the file size and ensure the overlay is the correct size. */
static int64_t
cow_get_size (struct nbdkit_next_ops *next_ops, void *nxdata,
              void *handle)
{
  int64_t size;

  size = next_ops->get_size (nxdata);
  if (size == -1)
    return -1;

  nbdkit_debug ("cow: underlying file size: %" PRIi64, size);

  if (blk_set_size (size))
    return -1;

  return size;
}

/* Force an early call to cow_get_size, consequently truncating the
 * overlay to the correct size.
 */
static int
cow_prepare (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle)
{
  int64_t r;

  r = cow_get_size (next_ops, nxdata, handle);
  return r >= 0 ? 0 : -1;
}

/* Whatever the underlying plugin can or can't do, we can write, we
 * cannot trim, and we can flush.
 */
static int
cow_can_write (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  return 1;
}

static int
cow_can_trim (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  return 0;
}

static int
cow_can_flush (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  return 1;
}

/* Return true if the block is allocated.  Consults the bitmap. */
static bool
blk_is_allocated (uint64_t blknum)
{
  uint64_t bm_offset = blknum / 8;
  uint64_t bm_bit = blknum % 8;

  if (bm_offset >= bm_size) {
    nbdkit_debug ("blk_is_allocated: block number is out of range");
    return false;
  }

  return bitmap[bm_offset] & (1 << bm_bit);
}

/* Mark a block as allocated. */
static void
blk_set_allocated (uint64_t blknum)
{
  uint64_t bm_offset = blknum / 8;
  uint64_t bm_bit = blknum % 8;

  if (bm_offset >= bm_size) {
    nbdkit_debug ("blk_set_allocated: block number is out of range");
    return;
  }

  bitmap[bm_offset] |= 1 << bm_bit;
}

/* These are the block operations.  They always read or write a single
 * whole block of size ‘blksize’.
 */
static int
blk_read (struct nbdkit_next_ops *next_ops, void *nxdata,
          uint64_t blknum, uint8_t *block)
{
  off_t offset = blknum * BLKSIZE;
  bool allocated = blk_is_allocated (blknum);

  nbdkit_debug ("cow: blk_read block %" PRIu64 " (offset %" PRIu64 ") is %s",
                blknum, (uint64_t) offset,
                !allocated ? "a hole" : "allocated");

  if (!allocated)               /* Read underlying plugin. */
    return next_ops->pread (nxdata, block, BLKSIZE, offset);
  else {                        /* Read overlay. */
    if (pread (fd, block, BLKSIZE, offset) == -1) {
      nbdkit_error ("pread: %m");
      return -1;
    }
    return 0;
  }
}

static int
blk_write (uint64_t blknum, const uint8_t *block)
{
  off_t offset = blknum * BLKSIZE;

  nbdkit_debug ("cow: blk_write block %" PRIu64 " (offset %" PRIu64 ")",
                blknum, (uint64_t) offset);

  if (pwrite (fd, block, BLKSIZE, offset) == -1) {
    nbdkit_error ("pwrite: %m");
    return -1;
  }
  blk_set_allocated (blknum);

  return 0;
}

/* Read data. */
static int
cow_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
           void *handle, void *buf, uint32_t count, uint64_t offset)
{
  uint8_t *block;

  block = malloc (BLKSIZE);
  if (block == NULL) {
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

    if (blk_read (next_ops, nxdata, blknum, block) == -1) {
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
cow_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle, const void *buf, uint32_t count, uint64_t offset)
{
  uint8_t *block;

  block = malloc (BLKSIZE);
  if (block == NULL) {
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

    /* Do a read-modify-write operation on the current block. */
    if (blk_read (next_ops, nxdata, blknum, block) == -1) {
      free (block);
      return -1;
    }
    memcpy (&block[blkoffs], buf, n);
    if (blk_write (blknum, block) == -1) {
      free (block);
      return -1;
    }

    buf += n;
    count -= n;
    offset += n;
  }

  free (block);
  return 0;
}

/* Zero data. */
static int
cow_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
          void *handle, uint32_t count, uint64_t offset, int may_trim)
{
  uint8_t *block;

  block = malloc (BLKSIZE);
  if (block == NULL) {
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

    /* XXX There is the possibility of optimizing this: ONLY if we are
     * writing a whole, aligned block, then use FALLOC_FL_ZERO_RANGE.
     */
    if (blk_read (next_ops, nxdata, blknum, block) == -1) {
      free (block);
      return -1;
    }
    memset (&block[blkoffs], 0, n);
    if (blk_write (blknum, block) == -1) {
      free (block);
      return -1;
    }

    count -= n;
    offset += n;
  }

  free (block);
  return 0;
}

static int
cow_flush (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
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

static struct nbdkit_filter filter = {
  .name              = "cow",
  .longname          = "nbdkit copy-on-write (COW) filter",
  .version           = PACKAGE_VERSION,
  .load              = cow_load,
  .unload            = cow_unload,
  .open              = cow_open,
  .prepare           = cow_prepare,
  .get_size          = cow_get_size,
  .can_write         = cow_can_write,
  .can_flush         = cow_can_flush,
  .can_trim          = cow_can_trim,
  .pread             = cow_pread,
  .pwrite            = cow_pwrite,
  .zero              = cow_zero,
  .flush             = cow_flush,
};

NBDKIT_REGISTER_FILTER(filter)
