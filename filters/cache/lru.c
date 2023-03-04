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

/* These are the block operations.  They always read or write a single
 * whole block of size ‘blksize’.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#include <nbdkit-filter.h>

#include "bitmap.h"
#include "minmax.h"

#include "cache.h"
#include "blk.h"
#include "lru.h"

/* LRU bitmaps.  These bitmaps implement a simple, fast LRU structure.
 *
 *    bm[0]
 * ┌───────────────────────┐
 * │  X    XX   X   XXX    │ c0 bits set
 * └───────────────────────┘
 *    bm[1]
 * ┌───────────────────────┐
 * │   X    XX  X   X      │ c1 bits set
 * └───────────────────────┘
 *
 * The LRU structure keeps track of the [approx] last N distinct
 * blocks which have been most recently accessed.  It can answer in
 * O(1) time the question: “Is a particular block in or not in the N
 * distinct blocks most recently accessed?”
 *
 * To do this we keep two bitmaps.
 *
 * When a new block is accessed, we set the corresponding bit in bm[0]
 * and increment c0 (c0 counts the number of bits set in bm[0]).  If
 * c0 == N/2 then we move bm[1] <- bm[0], clear bm[0] and set c0 <- 0.
 *
 * To check if a block has been accessed within the previous N
 * distinct accesses, we simply have to check both bitmaps.  If it is
 * not in either bitmap, then it's old and a candidate to be
 * reclaimed.
 *
 * You'll note that in fact we only keep track of between N/2 and N
 * recently accessed blocks because the same block can appear in both
 * bitmaps.  bm[1] is a last chance to hold on to blocks which are
 * soon to be reclaimed.  We could make the estimate more accurate by
 * having more bitmaps, but as this is only a heuristic we choose to
 * keep the implementation simple and memory usage low instead.
 */
static struct bitmap bm[2];
static unsigned c0 = 0, c1 = 0;
static unsigned N = 100;

void
lru_init (void)
{
  bitmap_init (&bm[0], blksize, 1 /* bits per block */);
  bitmap_init (&bm[1], blksize, 1 /* bits per block */);
}

void
lru_free (void)
{
  bitmap_free (&bm[0]);
  bitmap_free (&bm[1]);
}

int
lru_set_size (uint64_t new_size)
{
  if (bitmap_resize (&bm[0], new_size) == -1)
    return -1;
  if (bitmap_resize (&bm[1], new_size) == -1)
    return -1;

  if (max_size != -1)
    /* Make the threshold about 1/4 the maximum size of the cache. */
    N = MAX (max_size / blksize / 4, 100);
  else
    N = MAX (new_size / blksize / 4, 100);

  return 0;
}

void
lru_set_recently_accessed (uint64_t blknum)
{
  /* If the block is already set in the first bitmap, don't need to do
   * anything.
   */
  if (bitmap_get_blk (&bm[0], blknum, false))
    return;

  bitmap_set_blk (&bm[0], blknum, true);
  c0++;

  /* If we've reached N/2 then we need to swap over the bitmaps.  Note
   * the purpose of swapping here is to ensure that we do not have to
   * copy the dynamically allocated bm->bitmap field (the pointers are
   * swapped instead).  The bm[0].bitmap field is immediately zeroed
   * after the swap.
   */
  if (c0 >= N/2) {
    struct bitmap tmp;

    tmp = bm[0];
    bm[0] = bm[1];
    bm[1] = tmp;
    c1 = c0;

    bitmap_clear (&bm[0]);
    c0 = 0;
  }
}

bool
lru_has_been_recently_accessed (uint64_t blknum)
{
  return
    bitmap_get_blk (&bm[0], blknum, false) ||
    bitmap_get_blk (&bm[1], blknum, false);
}
