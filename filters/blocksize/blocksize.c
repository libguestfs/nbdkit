/* nbdkit
 * Copyright (C) 2018-2022 Red Hat Inc.
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
#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>
#include <errno.h>
#include <pthread.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "ispowerof2.h"
#include "minmax.h"
#include "rounding.h"

#define BLOCKSIZE_MIN_LIMIT (64U * 1024)

/* In order to handle parallel requests safely, this lock must be held
 * when using the bounce buffer.
 */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* A single bounce buffer for alignment purposes, guarded by the lock.
 * Size it to the maximum we allow for minblock.
 */
static char bounce[BLOCKSIZE_MIN_LIMIT];

static unsigned int minblock;
static unsigned int maxdata;
static unsigned int maxlen;

static int
blocksize_parse (const char *name, const char *s, unsigned int *v)
{
  int64_t size = nbdkit_parse_size (s);

  if (size < 0)
    return -1;
  if (!size) {
    nbdkit_error ("parameter '%s' must be non-zero if specified", name);
    return -1;
  }
  if (UINT_MAX < size) {
    nbdkit_error ("parameter '%s' too large", name);
    return -1;
  }
  *v = size;
  return 0;
}

/* Called for each key=value passed on the command line. */
static int
blocksize_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
                  const char *key, const char *value)
{

  if (strcmp (key, "minblock") == 0)
    return blocksize_parse (key, value, &minblock);
  if (strcmp (key, "maxdata") == 0)
    return blocksize_parse (key, value, &maxdata);
  if (strcmp (key, "maxlen") == 0)
    return blocksize_parse (key, value, &maxlen);
  return next (nxdata, key, value);
}

/* Check that limits are sane. */
static int
blocksize_config_complete (nbdkit_next_config_complete *next,
                           nbdkit_backend *nxdata)
{
  if (minblock) {
    if (!is_power_of_2 (minblock)) {
      nbdkit_error ("minblock must be a power of 2");
      return -1;
    }
    if (minblock > BLOCKSIZE_MIN_LIMIT) {
      nbdkit_error ("minblock must not exceed %u", BLOCKSIZE_MIN_LIMIT);
      return -1;
    }
  }
  else
    minblock = 1;

  if (maxdata) {
    if (maxdata & (minblock - 1)) {
      nbdkit_error ("maxdata must be a multiple of %u", minblock);
      return -1;
    }
  }
  else if (maxlen)
    maxdata = MIN (maxlen, 64 * 1024 * 1024);
  else
    maxdata = 64 * 1024 * 1024;

  if (maxlen) {
    if (maxlen & (minblock - 1)) {
      nbdkit_error ("maxlen must be a multiple of %u", minblock);
      return -1;
    }
  }
  else
    maxlen = -minblock;

  return next (nxdata);
}

#define blocksize_config_help \
  "minblock=<SIZE>      Minimum block size, power of 2 <= 64k (default 1).\n" \
  "maxdata=<SIZE>       Maximum size for read/write (default 64M).\n" \
  "maxlen=<SIZE>        Maximum size for trim/zero (default 4G-minblock)."

/* Round size down to avoid issues at end of file. */
static int64_t
blocksize_get_size (nbdkit_next *next,
                    void *handle)
{
  int64_t size = next->get_size (next);

  if (size == -1)
    return -1;
  return ROUND_DOWN (size, minblock);
}

static int
blocksize_pread (nbdkit_next *next,
                 void *handle, void *b, uint32_t count, uint64_t offs,
                 uint32_t flags, int *err)
{
  char *buf = b;
  uint32_t keep;
  uint32_t drop;

  /* Unaligned head */
  if (offs & (minblock - 1)) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    drop = offs & (minblock - 1);
    keep = MIN (minblock - drop, count);
    if (next->pread (next, bounce, minblock, offs - drop, flags, err) == -1)
      return -1;
    memcpy (buf, bounce + drop, keep);
    buf += keep;
    offs += keep;
    count -= keep;
  }

  /* Aligned body */
  while (count >= minblock) {
    keep = MIN (maxdata, ROUND_DOWN (count, minblock));
    if (next->pread (next, buf, keep, offs, flags, err) == -1)
      return -1;
    buf += keep;
    offs += keep;
    count -= keep;
  }

  /* Unaligned tail */
  if (count) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    if (next->pread (next, bounce, minblock, offs, flags, err) == -1)
      return -1;
    memcpy (buf, bounce, count);
  }

  return 0;
}

static int
blocksize_pwrite (nbdkit_next *next,
                  void *handle, const void *b, uint32_t count, uint64_t offs,
                  uint32_t flags, int *err)
{
  const char *buf = b;
  uint32_t keep;
  uint32_t drop;
  bool need_flush = false;

  if ((flags & NBDKIT_FLAG_FUA) &&
      next->can_fua (next) == NBDKIT_FUA_EMULATE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }

  /* Unaligned head */
  if (offs & (minblock - 1)) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    drop = offs & (minblock - 1);
    keep = MIN (minblock - drop, count);
    if (next->pread (next, bounce, minblock, offs - drop, 0, err) == -1)
      return -1;
    memcpy (bounce + drop, buf, keep);
    if (next->pwrite (next, bounce, minblock, offs - drop, flags, err) == -1)
      return -1;
    buf += keep;
    offs += keep;
    count -= keep;
  }

  /* Aligned body */
  while (count >= minblock) {
    keep = MIN (maxdata, ROUND_DOWN (count, minblock));
    if (next->pwrite (next, buf, keep, offs, flags, err) == -1)
      return -1;
    buf += keep;
    offs += keep;
    count -= keep;
  }

  /* Unaligned tail */
  if (count) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    if (next->pread (next, bounce, minblock, offs, 0, err) == -1)
      return -1;
    memcpy (bounce, buf, count);
    if (next->pwrite (next, bounce, minblock, offs, flags, err) == -1)
      return -1;
  }

  if (need_flush)
    return next->flush (next, 0, err);
  return 0;
}

static int
blocksize_trim (nbdkit_next *next,
                void *handle, uint32_t count, uint64_t offs, uint32_t flags,
                int *err)
{
  uint32_t keep;
  bool need_flush = false;

  if ((flags & NBDKIT_FLAG_FUA) &&
      next->can_fua (next) == NBDKIT_FUA_EMULATE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }

  /* Ignore unaligned head */
  if (offs & (minblock - 1)) {
    keep = MIN (minblock - (offs & (minblock - 1)), count);
    offs += keep;
    count -= keep;
  }

  /* Ignore unaligned tail */
  count = ROUND_DOWN (count, minblock);

  /* Aligned body */
  while (count) {
    keep = MIN (maxlen, count);
    if (next->trim (next, keep, offs, flags, err) == -1)
      return -1;
    offs += keep;
    count -= keep;
  }

  if (need_flush)
    return next->flush (next, 0, err);
  return 0;
}

static int
blocksize_zero (nbdkit_next *next,
                void *handle, uint32_t count, uint64_t offs, uint32_t flags,
                int *err)
{
  uint32_t keep;
  uint32_t drop;
  bool need_flush = false;

  if (flags & NBDKIT_FLAG_FAST_ZERO) {
    /* If we have to split the transaction, an ENOTSUP fast failure in
     * a later call would be unnecessarily delayed behind earlier
     * calls; it's easier to just declare that anything that can't be
     * done in one call to the plugin is not fast.
     */
    if ((offs | count) & (minblock - 1) || count > maxlen) {
      *err = ENOTSUP;
      return -1;
    }
  }

  if ((flags & NBDKIT_FLAG_FUA) &&
      next->can_fua (next) == NBDKIT_FUA_EMULATE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }

  /* Unaligned head */
  if (offs & (minblock - 1)) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    drop = offs & (minblock - 1);
    keep = MIN (minblock - drop, count);
    if (next->pread (next, bounce, minblock, offs - drop, 0, err) == -1)
      return -1;
    memset (bounce + drop, 0, keep);
    if (next->pwrite (next, bounce, minblock, offs - drop,
                      flags & ~NBDKIT_FLAG_MAY_TRIM, err) == -1)
      return -1;
    offs += keep;
    count -= keep;
  }

  /* Aligned body */
  while (count >= minblock) {
    keep = MIN (maxlen, ROUND_DOWN (count, minblock));
    if (next->zero (next, keep, offs, flags, err) == -1)
      return -1;
    offs += keep;
    count -= keep;
  }

  /* Unaligned tail */
  if (count) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    if (next->pread (next, bounce, minblock, offs, 0, err) == -1)
      return -1;
    memset (bounce, 0, count);
    if (next->pwrite (next, bounce, minblock, offs,
                      flags & ~NBDKIT_FLAG_MAY_TRIM, err) == -1)
      return -1;
  }

  if (need_flush)
    return next->flush (next, 0, err);
  return 0;
}

static int
blocksize_extents (nbdkit_next *next,
                   void *handle, uint32_t count, uint64_t offset,
                   uint32_t flags, struct nbdkit_extents *extents, int *err)
{
  /* Ask the plugin for blocksize-aligned data.  Copying that into the
   * callers' extents will then take care of truncating unaligned
   * ends.  Also we only need to ask for maxlen of data, because it's
   * fine to return less than the full count as long as we're making
   * progress.
   */
  CLEANUP_EXTENTS_FREE struct nbdkit_extents *extents2 = NULL;
  size_t i;
  struct nbdkit_extent e;

  extents2 = nbdkit_extents_new (ROUND_DOWN (offset, minblock),
                                 ROUND_UP (offset + count, minblock));
  if (extents2 == NULL) {
    *err = errno;
    return -1;
  }

  if (nbdkit_extents_aligned (next, MIN (ROUND_UP (count, minblock), maxlen),
                              ROUND_DOWN (offset, minblock), flags, minblock,
                              extents2, err) == -1)
    return -1;

  for (i = 0; i < nbdkit_extents_count (extents2); ++i) {
    e = nbdkit_get_extent (extents2, i);
    if (nbdkit_add_extent (extents, e.offset, e.length, e.type) == -1) {
      *err = errno;
      return -1;
    }
  }
  return 0;
}

static int
blocksize_cache (nbdkit_next *next,
                 void *handle, uint32_t count, uint64_t offs, uint32_t flags,
                 int *err)
{
  uint32_t limit;
  uint64_t remaining = count; /* Rounding out could exceed 32 bits */

  /* Unaligned head */
  limit = offs & (minblock - 1);
  remaining += limit;
  offs -= limit;

  /* Unaligned tail */
  remaining = ROUND_UP (remaining, minblock);

  /* Aligned body */
  while (remaining) {
    limit = MIN (maxdata, remaining);
    if (next->cache (next, limit, offs, flags, err) == -1)
      return -1;
    offs += limit;
    remaining -= limit;
  }

  return 0;
}

static struct nbdkit_filter filter = {
  .name              = "blocksize",
  .longname          = "nbdkit blocksize filter",
  .config            = blocksize_config,
  .config_complete   = blocksize_config_complete,
  .config_help       = blocksize_config_help,
  .get_size          = blocksize_get_size,
  .pread             = blocksize_pread,
  .pwrite            = blocksize_pwrite,
  .trim              = blocksize_trim,
  .zero              = blocksize_zero,
  .extents           = blocksize_extents,
  .cache             = blocksize_cache,
};

NBDKIT_REGISTER_FILTER(filter)
