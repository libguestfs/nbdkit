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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <nbdkit-filter.h>

#include "bitmap.h"

#include "cache.h"
#include "reclaim.h"
#include "lru.h"

#ifndef HAVE_CACHE_RECLAIM

void
reclaim (int fd, struct bitmap *bm)
{
  /* nothing */
}

#else /* HAVE_CACHE_RECLAIM */

/* If we are currently reclaiming blocks from the cache.
 *
 * The state machine starts in the NOT_RECLAIMING state.  When the
 * size of the cache exceeds the high threshold, we move to
 * RECLAIMING_LRU.  Once we have exhausted all LRU blocks, we move to
 * RECLAIMING_ANY (reclaiming any blocks).
 *
 * If at any time the size of the cache goes below the low threshold
 * we move back to the NOT_RECLAIMING state.
 *
 * A possible future enhancement is to add an extra state between LRU
 * and ANY which reclaims blocks from lru.c:bm[1].
 *
 * reclaim_blk is the last block that we looked at.
 */
enum reclaim_state {
  NOT_RECLAIMING = 0,
  RECLAIMING_LRU = 1,
  RECLAIMING_ANY = 2,
};

static enum reclaim_state reclaiming = NOT_RECLAIMING;
static int64_t reclaim_blk;

static void reclaim_one (int fd, struct bitmap *bm);
static void reclaim_lru (int fd, struct bitmap *bm);
static void reclaim_any (int fd, struct bitmap *bm);
static void reclaim_block (int fd, struct bitmap *bm);

void
reclaim (int fd, struct bitmap *bm)
{
  struct stat statbuf;
  uint64_t cache_allocated;

  /* If the user didn't set cache-max-size, do nothing. */
  if (max_size == -1) return;

  /* Check the allocated size of the cache. */
  if (fstat (fd, &statbuf) == -1) {
    nbdkit_debug ("cache: fstat: %m");
    return;
  }
  cache_allocated = statbuf.st_blocks * UINT64_C (512);

  if (reclaiming) {
    /* Keep reclaiming until the cache size drops below the low threshold. */
    if (cache_allocated < max_size * lo_thresh / 100) {
      nbdkit_debug ("cache: stop reclaiming");
      reclaiming = NOT_RECLAIMING;
      return;
    }
  }
  else {
    if (cache_allocated < max_size * hi_thresh / 100)
      return;

    /* Start reclaiming if the cache size goes over the high threshold. */
    nbdkit_debug ("cache: start reclaiming");
    reclaiming = RECLAIMING_LRU;
  }

  /* Reclaim up to 2 cache blocks. */
  reclaim_one (fd, bm);
  reclaim_one (fd, bm);
}

/* Reclaim a single cache block. */
static void
reclaim_one (int fd, struct bitmap *bm)
{
  assert (reclaiming);

  if (reclaiming == RECLAIMING_LRU)
    reclaim_lru (fd, bm);
  else
    reclaim_any (fd, bm);
}

static void
reclaim_lru (int fd, struct bitmap *bm)
{
  int64_t old_reclaim_blk;

  /* Find the next block in the cache. */
  reclaim_blk = bitmap_next (bm, reclaim_blk+1);
  old_reclaim_blk = reclaim_blk;

  /* Search for an LRU block after this one. */
  do {
    if (! lru_has_been_recently_accessed (reclaim_blk)) {
      reclaim_block (fd, bm);
      return;
    }

    reclaim_blk = bitmap_next (bm, reclaim_blk+1);
    if (reclaim_blk == -1)    /* wrap around */
      reclaim_blk = bitmap_next (bm, 0);
  } while (reclaim_blk >= 0 && old_reclaim_blk != reclaim_blk);

  if (old_reclaim_blk == reclaim_blk) {
    /* Run out of LRU blocks, so start reclaiming any block in the cache. */
    nbdkit_debug ("cache: reclaiming any blocks");
    reclaiming = RECLAIMING_ANY;
    reclaim_any (fd, bm);
  }
}

static void
reclaim_any (int fd, struct bitmap *bm)
{
  /* Find the next block in the cache. */
  reclaim_blk = bitmap_next (bm, reclaim_blk+1);
  if (reclaim_blk == -1)        /* wrap around */
    reclaim_blk = bitmap_next (bm, 0);

  reclaim_block (fd, bm);
}

static void
reclaim_block (int fd, struct bitmap *bm)
{
  if (reclaim_blk == -1) {
    nbdkit_debug ("cache: run out of blocks to reclaim!");
    return;
  }

  nbdkit_debug ("cache: reclaiming block %" PRIu64, reclaim_blk);
#ifdef FALLOC_FL_PUNCH_HOLE
  if (fallocate (fd, FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE,
                 reclaim_blk * blksize, blksize) == -1) {
    nbdkit_error ("cache: reclaiming cache blocks: "
                  "fallocate: FALLOC_FL_PUNCH_HOLE: %m");
    return;
  }
#else
#error "no implementation for punching holes"
#endif

  bitmap_set_blk (bm, reclaim_blk, 0);
}

#endif /* HAVE_CACHE_RECLAIM */
