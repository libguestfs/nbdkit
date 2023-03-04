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

/* This is a very simple structure for creating a bitmap associated
 * with a virtual disk.  1, 2, 4 or 8 bits can be associated with each
 * block of the disk.  You can choose the number of bits and block
 * size when creating the bitmap.  Entries in the bitmap are
 * initialized to 0.
 */

#ifndef NBDKIT_BITMAP_H
#define NBDKIT_BITMAP_H

#include <stdint.h>
#include <string.h>
#include <assert.h>

#include <nbdkit-plugin.h>

#include "ispowerof2.h"

/* This is the bitmap structure. */
struct bitmap {
  unsigned blksize;            /* Block size. */
  uint8_t bpb;                 /* Bits per block (1, 2, 4, 8 only). */
  /* bpb = 1 << bitshift   ibpb = 8 / bpb
     1          0          8
     2          1          4
     4          2          2
     8          3          1
  */
  uint8_t bitshift, ibpb;

  uint8_t *bitmap;              /* The bitmap. */
  size_t size;                  /* Size of bitmap in bytes. */
};

static inline void __attribute__ ((__nonnull__ (1)))
bitmap_init (struct bitmap *bm, unsigned blocksize, unsigned bpb)
{
  assert (is_power_of_2 (blocksize));
  bm->blksize = blocksize;

  /* bpb can be 1, 2, 4 or 8 only. */
  bm->bpb = bpb;
  switch (bpb) {
  case 1: bm->bitshift = 0; break;
  case 2: bm->bitshift = 1; break;
  case 4: bm->bitshift = 2; break;
  case 8: bm->bitshift = 3; break;
  default: abort ();
  }
  bm->ibpb = 8/bpb;

  bm->bitmap = NULL;
  bm->size = 0;
}

/* Only frees the bitmap itself, since it is assumed that the struct
 * bitmap is statically allocated.
 */
static inline void
bitmap_free (struct bitmap *bm)
{
  if (bm)
    free (bm->bitmap);
}

/* Resize the bitmap to the virtual disk size in bytes.
 * Returns -1 on error, setting nbdkit_error.
 */
extern int bitmap_resize (struct bitmap *bm, uint64_t new_size)
  __attribute__ ((__nonnull__ (1)));

/* Clear the bitmap (set everything to zero). */
static inline void  __attribute__ ((__nonnull__ (1)))
bitmap_clear (struct bitmap *bm)
{
  memset (bm->bitmap, 0, bm->size);
}

/* This macro calculates the byte offset in the bitmap and which
 * bit/mask we are addressing within that byte.
 *
 * bpb     blk_offset         blk_bit          mask
 * 1       blk >> 3           0,1,2,...,7      any single bit
 * 2       blk >> 2           0, 2, 4 or 6     0x03, 0x0c, 0x30 or 0xc0
 * 4       blk >> 1           0 or 4           0x0f or 0xf0
 * 8       blk >> 0           always 0         always 0xff
 */
#define BITMAP_OFFSET_BIT_MASK(bm, blk)                         \
  uint64_t blk_offset = (blk) >> (3 - (bm)->bitshift);          \
  unsigned blk_bit = (bm)->bpb * ((blk) & ((bm)->ibpb - 1));    \
  unsigned mask = ((1 << (bm)->bpb) - 1) << blk_bit

/* Return the bit(s) associated with the given block.
 * If the request is out of range, returns the default value.
 */
static inline unsigned __attribute__ ((__nonnull__ (1)))
bitmap_get_blk (const struct bitmap *bm, uint64_t blk, unsigned default_)
{
  BITMAP_OFFSET_BIT_MASK (bm, blk);

  if (blk_offset >= bm->size) {
    nbdkit_debug ("bitmap_get: block number is out of range");
    return default_;
  }

  return (bm->bitmap[blk_offset] & mask) >> blk_bit;
}

/* As above but works with virtual disk offset in bytes. */
static inline unsigned __attribute__ ((__nonnull__ (1)))
bitmap_get (const struct bitmap *bm, uint64_t offset, unsigned default_)
{
  return bitmap_get_blk (bm, offset / bm->blksize, default_);
}

/* Set the bit(s) associated with the given block.
 * If out of range, it is ignored.
 */
static inline void __attribute__ ((__nonnull__ (1)))
bitmap_set_blk (const struct bitmap *bm, uint64_t blk, unsigned v)
{
  BITMAP_OFFSET_BIT_MASK (bm, blk);

  if (blk_offset >= bm->size) {
    nbdkit_debug ("bitmap_set: block number is out of range");
    return;
  }

  bm->bitmap[blk_offset] &= ~mask;
  bm->bitmap[blk_offset] |= v << blk_bit;
}

/* As above bit works with virtual disk offset in bytes. */
static inline void __attribute__ ((__nonnull__ (1)))
bitmap_set (const struct bitmap *bm, uint64_t offset, unsigned v)
{
  return bitmap_set_blk (bm, offset / bm->blksize, v);
}

/* Iterate over blocks represented in the bitmap. */
#define bitmap_for(bm, /* uint64_t */ blknum)                           \
  for ((blknum) = 0; (blknum) < (bm)->size * (bm)->ibpb; ++(blknum))

/* Find the next non-zero block in the bitmap, starting at ‘blk’.
 * Returns -1 if the bitmap is all zeroes from blk to the end of the
 * bitmap.
 */
extern int64_t bitmap_next (const struct bitmap *bm, uint64_t blk)
  __attribute__ ((__nonnull__ (1)));

#endif /* NBDKIT_BITMAP_H */
