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
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include <pthread.h>

#include <nbdkit-filter.h>

#include "cleanup.h"

/* -D cacheextents.cache=1: Debug cache operations. */
NBDKIT_DLL_PUBLIC int cacheextents_debug_cache = 0;

/* This lock protects the global state. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* Cached extents from the last extents () call and its start and end
 * for the sake of simplicity.
 */
struct nbdkit_extents *cache_extents;
static uint64_t cache_start;
static uint64_t cache_end;

static void
cacheextents_unload (void)
{
  nbdkit_extents_free (cache_extents);
}

static int
cacheextents_add (struct nbdkit_extents *extents, int *err)
{
  size_t i = 0;

  for (i = 0; i < nbdkit_extents_count (cache_extents); i++) {
    struct nbdkit_extent ex = nbdkit_get_extent (cache_extents, i);
    if (nbdkit_add_extent (extents, ex.offset, ex.length, ex.type) == -1) {
      *err = errno;
      return -1;
    }
  }

  return 0;
}

static int
fill (struct nbdkit_extents *extents, int *err)
{
  size_t i = 0;
  size_t count = nbdkit_extents_count (extents);
  struct nbdkit_extent first = nbdkit_get_extent (extents, 0);
  struct nbdkit_extent last = nbdkit_get_extent (extents, count - 1);

  nbdkit_extents_free (cache_extents);
  cache_start = first.offset;
  cache_end = last.offset + last.length;
  cache_extents = nbdkit_extents_new (cache_start, cache_end);

  if (!cache_extents)
    return -1;

  for (i = 0; i < count; i++) {
    struct nbdkit_extent ex = nbdkit_get_extent (extents, i);

    if (cacheextents_debug_cache)
      nbdkit_debug ("cacheextents: updating cache with:"
                    " offset=%" PRIu64
                    " length=%" PRIu64
                    " type=%x",
                    ex.offset, ex.length, ex.type);

    if (nbdkit_add_extent (cache_extents, ex.offset, ex.length,
                           ex.type) == -1) {
      *err = errno;
      nbdkit_extents_free (cache_extents);
      cache_extents = NULL;
      return -1;
    }
  }

  return 0;
}

static int
cacheextents_extents (nbdkit_next *next,
                      void *handle, uint32_t count, uint64_t offset,
                      uint32_t flags,
                      struct nbdkit_extents *extents,
                      int *err)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);

  if (cacheextents_debug_cache)
    nbdkit_debug ("cacheextents:"
                  " cache_start=%" PRIu64
                  " cache_end=%" PRIu64
                  " cache_extents=%p",
                  cache_start, cache_end, cache_extents);

  if (cache_extents &&
      offset >= cache_start && offset < cache_end) {
    if (cacheextents_debug_cache)
      nbdkit_debug ("cacheextents: returning from cache");
    return cacheextents_add (extents, err);
  }

  if (cacheextents_debug_cache)
    nbdkit_debug ("cacheextents: cache miss");

  /* Clear REQ_ONE to ask the plugin for as much information as it is
   * willing to return (the plugin may still truncate if it is too
   * costly to provide everything).
   */
  flags &= ~(NBDKIT_FLAG_REQ_ONE);
  if (next->extents (next, count, offset, flags, extents, err) == -1)
    return -1;

  return fill (extents, err);
}

/* Any changes to the data needs to clean the cache.
 *
 * Similar to the readahead filter this could be more intelligent, but
 * there would be very little benefit.
 */

static void
kill_cacheextents (void)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
  nbdkit_extents_free (cache_extents);
  cache_extents = NULL;
}

static int
cacheextents_pwrite (nbdkit_next *next,
                     void *handle,
                     const void *buf, uint32_t count, uint64_t offset,
                     uint32_t flags, int *err)
{
  kill_cacheextents ();
  return next->pwrite (next, buf, count, offset, flags, err);
}

static int
cacheextents_trim (nbdkit_next *next,
                   void *handle,
                   uint32_t count, uint64_t offset, uint32_t flags,
                   int *err)
{
  kill_cacheextents ();
  return next->trim (next, count, offset, flags, err);
}

static int
cacheextents_zero (nbdkit_next *next,
                   void *handle,
                   uint32_t count, uint64_t offset, uint32_t flags,
                   int *err)
{
  kill_cacheextents ();
  return next->zero (next, count, offset, flags, err);
}

static struct nbdkit_filter filter = {
  .name              = "cacheextents",
  .longname          = "nbdkit cacheextents filter",
  .unload            = cacheextents_unload,
  .pwrite            = cacheextents_pwrite,
  .trim              = cacheextents_trim,
  .zero              = cacheextents_zero,
  .extents           = cacheextents_extents,
};

NBDKIT_REGISTER_FILTER (filter)
