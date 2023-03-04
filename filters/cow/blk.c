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
 * A 2-bit per block bitmap is maintained in memory recording if each
 * block in the temporary file is:
 *
 *   00 = not allocated in the overlay (read through to the plugin)
 *   01 = allocated in the overlay
 *   10 = <unused>
 *   11 = trimmed in the overlay
 *
 * When reading a block we first check the bitmap to see if that file
 * block is allocated, trimmed or not.  If allocated, we return it
 * from the temporary file.  Trimmed returns zeroes.  If not allocated
 * we issue a pread to the underlying plugin.
 *
 * When writing a block we unconditionally write the data to the
 * temporary file, setting the bit in the bitmap.  (Writing zeroes is
 * handled the same way.)
 *
 * When trimming we set the trimmed flag in the bitmap for whole
 * blocks, and handle the unaligned portions like writing zeroes
 * above.  We could punch holes in the overlay as an optimization, but
 * for simplicity we do not do that yet.
 *
 * Since the overlay is a deleted temporary file, we can ignore FUA
 * and flush commands.
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
#include <limits.h>
#include <errno.h>
#include <sys/types.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <pthread.h>

#include <nbdkit-filter.h>

#include "bitmap.h"
#include "cleanup.h"
#include "fdatasync.h"
#include "rounding.h"
#include "pread.h"
#include "pwrite.h"
#include "utils.h"

#include "cow.h"
#include "blk.h"

/* The temporary overlay. */
static int fd = -1;

/* This lock protects the bitmap from parallel access. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* Bitmap. */
static struct bitmap bm;

enum bm_entry {
  BLOCK_NOT_ALLOCATED = 0,
  BLOCK_ALLOCATED = 1,
  BLOCK_TRIMMED = 3,
};

static const char *
state_to_string (enum bm_entry state)
{
  switch (state) {
  case BLOCK_NOT_ALLOCATED: return "not allocated";
  case BLOCK_ALLOCATED: return "allocated";
  case BLOCK_TRIMMED: return "trimmed";
  default: abort ();
  }
}

/* Extra debugging (-D cow.verbose=1). */
NBDKIT_DLL_PUBLIC int cow_debug_verbose = 0;

int
blk_init (void)
{
  const char *tmpdir;
  size_t len;
  char *template;

  bitmap_init (&bm, blksize, 2 /* bits per block */);

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

/* Because blk_set_size is called before the other blk_* functions
 * this should be set to the true size before we need it.
 */
static uint64_t size = 0;

/* Allocate or resize the overlay file and bitmap. */
int
blk_set_size (uint64_t new_size)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);

  size = new_size;

  if (bitmap_resize (&bm, size) == -1)
    return -1;

  if (ftruncate (fd, ROUND_UP (size, blksize)) == -1) {
    nbdkit_error ("ftruncate: %m");
    return -1;
  }

  return 0;
}

/* This is a bit of a hack since usually this information is hidden in
 * the blk module.  However it is needed when calculating extents.
 */
void
blk_status (uint64_t blknum, bool *present, bool *trimmed)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
  enum bm_entry state = bitmap_get_blk (&bm, blknum, BLOCK_NOT_ALLOCATED);

  *present = state != BLOCK_NOT_ALLOCATED;
  *trimmed = state == BLOCK_TRIMMED;
}

/* These are the block operations.  They always read or write whole
 * blocks of size ‘blksize’.
 */
int
blk_read_multiple (nbdkit_next *next,
                   uint64_t blknum, uint64_t nrblocks,
                   uint8_t *block, bool cow_on_read, int *err)
{
  off_t offset = blknum * blksize;
  enum bm_entry state;
  uint64_t b, runblocks;

  /* Find out how many of the following blocks form a "run" with the
   * same state.  We can process that many blocks in one go.
   *
   * About the locking: The state might be modified from another
   * thread - for example another thread might write
   * (BLOCK_NOT_ALLOCATED -> BLOCK_ALLOCATED) while we are reading
   * from the plugin, returning the old data.  However a read issued
   * after the write returns should always return the correct data.
   */
  {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    state = bitmap_get_blk (&bm, blknum, BLOCK_NOT_ALLOCATED);

    for (b = 1, runblocks = 1; b < nrblocks; ++b, ++runblocks) {
      enum bm_entry s = bitmap_get_blk (&bm, blknum + b, BLOCK_NOT_ALLOCATED);
      if (state != s)
        break;
    }
  }

  if (cow_debug_verbose)
    nbdkit_debug ("cow: blk_read_multiple block %" PRIu64
                  " (offset %" PRIu64 ") run of length %" PRIu64
                  " is %s",
                  blknum, (uint64_t) offset, runblocks,
                  state_to_string (state));

  if (state == BLOCK_NOT_ALLOCATED) { /* Read underlying plugin. */
    unsigned n, tail = 0;

    assert (blksize * runblocks <= UINT_MAX);
    n = blksize * runblocks;

    if (offset + n > size) {
      tail = offset + n - size;
      n -= tail;
    }

    if (next->pread (next, block, n, offset, 0, err) == -1)
      return -1;

    /* Normally we're reading whole blocks, but at the very end of the
     * file we might read a partial block.  Deal with that case by
     * zeroing the tail.
     */
    memset (block + n, 0, tail);

    /* If cow-on-read is true then copy the blocks to the cache and
     * set them as allocated.
     */
    if (cow_on_read) {
      if (cow_debug_verbose)
        nbdkit_debug ("cow: cow-on-read saving %" PRIu64 " blocks "
                      "at offset %" PRIu64 " into the cache",
                      runblocks, offset);

      if (full_pwrite (fd, block, blksize * runblocks, offset) == -1) {
        *err = errno;
        nbdkit_error ("pwrite: %m");
        return -1;
      }
      for (b = 0; b < runblocks; ++b)
        bitmap_set_blk (&bm, blknum+b, BLOCK_ALLOCATED);
    }
  }
  else if (state == BLOCK_ALLOCATED) { /* Read overlay. */
    if (full_pread (fd, block, blksize * runblocks, offset) == -1) {
      *err = errno;
      nbdkit_error ("pread: %m");
      return -1;
    }
  }
  else /* state == BLOCK_TRIMMED */ {
    memset (block, 0, blksize * runblocks);
  }

  /* If all done, return. */
  if (runblocks == nrblocks)
    return 0;

  /* Recurse to read remaining blocks. */
  return blk_read_multiple (next,
                            blknum + runblocks,
                            nrblocks - runblocks,
                            block + blksize * runblocks,
                            cow_on_read, err);
}

int
blk_read (nbdkit_next *next,
          uint64_t blknum, uint8_t *block, bool cow_on_read, int *err)
{
  return blk_read_multiple (next, blknum, 1, block, cow_on_read, err);
}

int
blk_cache (nbdkit_next *next,
           uint64_t blknum, uint8_t *block, enum cache_mode mode, int *err)
{
  /* XXX Could make this lock more fine-grained with some thought. */
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
  off_t offset = blknum * blksize;
  enum bm_entry state = bitmap_get_blk (&bm, blknum, BLOCK_NOT_ALLOCATED);
  unsigned n = blksize, tail = 0;

  if (offset + n > size) {
    tail = offset + n - size;
    n -= tail;
  }

  if (cow_debug_verbose)
    nbdkit_debug ("cow: blk_cache block %" PRIu64 " (offset %" PRIu64 ") is %s",
                  blknum, (uint64_t) offset, state_to_string (state));

  if (state == BLOCK_ALLOCATED) {
#if HAVE_POSIX_FADVISE
    int r = posix_fadvise (fd, offset, blksize, POSIX_FADV_WILLNEED);
    if (r) {
      errno = r;
      nbdkit_error ("posix_fadvise: %m");
      return -1;
    }
#endif
    return 0;
  }
  if (state == BLOCK_TRIMMED)
    return 0;
  if (mode == BLK_CACHE_IGNORE)
    return 0;
  if (mode == BLK_CACHE_PASSTHROUGH)
    return next->cache (next, n, offset, 0, err);

  if (next->pread (next, block, n, offset, 0, err) == -1)
    return -1;
  /* Normally we're reading whole blocks, but at the very end of the
   * file we might read a partial block.  Deal with that case by
   * zeroing the tail.
   */
  memset (block + n, 0, tail);

  if (mode == BLK_CACHE_COW) {
    if (full_pwrite (fd, block, blksize, offset) == -1) {
      *err = errno;
      nbdkit_error ("pwrite: %m");
      return -1;
    }
    bitmap_set_blk (&bm, blknum, BLOCK_ALLOCATED);
  }
  return 0;
}

int
blk_write (uint64_t blknum, const uint8_t *block, int *err)
{
  off_t offset = blknum * blksize;

  if (cow_debug_verbose)
    nbdkit_debug ("cow: blk_write block %" PRIu64 " (offset %" PRIu64 ")",
                  blknum, (uint64_t) offset);

  if (full_pwrite (fd, block, blksize, offset) == -1) {
    *err = errno;
    nbdkit_error ("pwrite: %m");
    return -1;
  }

  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
  bitmap_set_blk (&bm, blknum, BLOCK_ALLOCATED);

  return 0;
}

int
blk_trim (uint64_t blknum, int *err)
{
  off_t offset = blknum * blksize;

  if (cow_debug_verbose)
    nbdkit_debug ("cow: blk_trim block %" PRIu64 " (offset %" PRIu64 ")",
                  blknum, (uint64_t) offset);

  /* XXX As an optimization we could punch a whole in the overlay
   * here.  However it's not trivial since blksize is unrelated to the
   * overlay filesystem block size.
   */
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
  bitmap_set_blk (&bm, blknum, BLOCK_TRIMMED);
  return 0;
}
