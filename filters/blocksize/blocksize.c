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

/* Lock in order to handle overlapping requests safely.
 *
 * Grabbed for exclusive access (wrlock) when using the bounce buffer.
 *
 * Grabbed for shared access (rdlock) when doing aligned writes.
 * These can happen in parallel with one another, but must not land in
 * between the read and write of an unaligned RMW operation.
 */
static pthread_rwlock_t lock = PTHREAD_RWLOCK_INITIALIZER;

/* A single bounce buffer for alignment purposes, guarded by the lock.
 * Size it to the maximum we allow for minblock.
 */
static char bounce[BLOCKSIZE_MIN_LIMIT];

/* Globals set by .config */
static unsigned int config_minblock;
static unsigned int config_maxdata;
static unsigned int config_maxlen;

/* Per-handle values set during .prepare */
struct blocksize_handle {
  uint32_t minblock;
  uint32_t maxdata;
  uint32_t maxlen;
};

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
    return blocksize_parse (key, value, &config_minblock);
  if (strcmp (key, "maxdata") == 0)
    return blocksize_parse (key, value, &config_maxdata);
  if (strcmp (key, "maxlen") == 0)
    return blocksize_parse (key, value, &config_maxlen);
  return next (nxdata, key, value);
}

/* Check that limits are sane. */
static int
blocksize_config_complete (nbdkit_next_config_complete *next,
                           nbdkit_backend *nxdata)
{
  if (config_minblock) {
    if (!is_power_of_2 (config_minblock)) {
      nbdkit_error ("minblock must be a power of 2");
      return -1;
    }
    if (config_minblock > BLOCKSIZE_MIN_LIMIT) {
      nbdkit_error ("minblock must not exceed %u", BLOCKSIZE_MIN_LIMIT);
      return -1;
    }
  }

  if (config_maxdata && config_minblock) {
    if (config_maxdata & (config_minblock - 1)) {
      nbdkit_error ("maxdata must be a multiple of %u", config_minblock);
      return -1;
    }
  }

  if (config_maxlen && config_minblock) {
    if (config_maxlen & (config_minblock - 1)) {
      nbdkit_error ("maxlen must be a multiple of %u", config_minblock);
      return -1;
    }
  }

  nbdkit_debug ("configured values minblock=%u maxdata=%u maxlen=%u",
                config_minblock, config_maxdata, config_maxlen);
  return next (nxdata);
}

#define blocksize_config_help \
  "minblock=<SIZE>      Minimum block size, power of 2 <= 64k (default 1).\n" \
  "maxdata=<SIZE>       Maximum size for read/write (default 64M).\n" \
  "maxlen=<SIZE>        Maximum size for trim/zero (default 4G-minblock)."

static void *
blocksize_open (nbdkit_next_open *next, nbdkit_context *nxdata,
                int readonly, const char *exportname, int is_tls)
{
  struct blocksize_handle *h;

  if (next (nxdata, readonly, exportname) == -1)
    return NULL;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  h->minblock = config_minblock;
  h->maxdata = config_maxdata;
  h->maxlen = config_maxlen;
  return h;
}

static int
blocksize_prepare (nbdkit_next *next, void *handle,
                   int readonly)
{
  struct blocksize_handle *h = handle;
  uint32_t minimum, preferred, maximum;

  /* Here, minimum and maximum will clamp per-handle defaults not set
   * by globals in .config; preferred has no impact until .block_size.
   */
  if (next->block_size (next, &minimum, &preferred, &maximum) == -1)
    return -1;

  h->minblock = MAX (MAX (h->minblock, 1), minimum);

  if (h->maxdata == 0) {
    if (h->maxlen)
      h->maxdata = MIN (h->maxlen, 64 * 1024 * 1024);
    else
      h->maxdata = 64 * 1024 * 1024;
  }
  if (maximum)
    h->maxdata = MIN (h->maxdata, maximum);
  h->maxdata = ROUND_DOWN (h->maxdata, h->minblock);

  if (h->maxlen == 0)
    h->maxlen = -h->minblock;
  else
    h->maxlen = ROUND_DOWN (h->maxlen, h->minblock);

  nbdkit_debug ("handle values minblock=%u maxdata=%u maxlen=%u",
                h->minblock, h->maxdata, h->maxlen);
  return 0;
}

/* Round size down to avoid issues at end of file. */
static int64_t
blocksize_get_size (nbdkit_next *next,
                    void *handle)
{
  struct blocksize_handle *h = handle;
  int64_t size = next->get_size (next);

  if (size == -1)
    return -1;
  return ROUND_DOWN (size, h->minblock);
}

/* Block size constraints.
 *
 * This filter is a little unusual because it allows clients to send a
 * wider range of request sizes than the underlying plugin allows.
 * Therefore we advertise the widest possible minimum and maximum
 * block size to clients.
 */
static int
blocksize_block_size (nbdkit_next *next, void *handle,
                      uint32_t *minimum, uint32_t *preferred, uint32_t *maximum)
{
  struct blocksize_handle *h = handle;

  /* Here we only need preferred; see also blocksize_prepare. */
  if (next->block_size (next, minimum, preferred, maximum) == -1)
    return -1;

  *preferred = MAX (MAX (*preferred, 4096), h->minblock);

  *minimum = 1;
  *maximum = 0xffffffff;

  nbdkit_debug ("advertising min=%" PRIu32 " pref=%" PRIu32 " max=%" PRIu32,
                *minimum, *preferred, *maximum);
  return 0;
}

static int
blocksize_pread (nbdkit_next *next,
                 void *handle, void *b, uint32_t count, uint64_t offs,
                 uint32_t flags, int *err)
{
  struct blocksize_handle *h = handle;
  char *buf = b;
  uint32_t keep;
  uint32_t drop;

  /* Unaligned head */
  if (offs & (h->minblock - 1)) {
    ACQUIRE_WRLOCK_FOR_CURRENT_SCOPE (&lock);
    drop = offs & (h->minblock - 1);
    keep = MIN (h->minblock - drop, count);
    if (next->pread (next, bounce, h->minblock, offs - drop, flags, err) == -1)
      return -1;
    memcpy (buf, bounce + drop, keep);
    buf += keep;
    offs += keep;
    count -= keep;
  }

  /* Aligned body */
  while (count >= h->minblock) {
    keep = MIN (h->maxdata, ROUND_DOWN (count, h->minblock));
    if (next->pread (next, buf, keep, offs, flags, err) == -1)
      return -1;
    buf += keep;
    offs += keep;
    count -= keep;
  }

  /* Unaligned tail */
  if (count) {
    ACQUIRE_WRLOCK_FOR_CURRENT_SCOPE (&lock);
    if (next->pread (next, bounce, h->minblock, offs, flags, err) == -1)
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
  struct blocksize_handle *h = handle;
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
  if (offs & (h->minblock - 1)) {
    ACQUIRE_WRLOCK_FOR_CURRENT_SCOPE (&lock);
    drop = offs & (h->minblock - 1);
    keep = MIN (h->minblock - drop, count);
    if (next->pread (next, bounce, h->minblock, offs - drop, 0, err) == -1)
      return -1;
    memcpy (bounce + drop, buf, keep);
    if (next->pwrite (next, bounce, h->minblock, offs - drop, flags, err) == -1)
      return -1;
    buf += keep;
    offs += keep;
    count -= keep;
  }

  /* Aligned body */
  while (count >= h->minblock) {
    ACQUIRE_RDLOCK_FOR_CURRENT_SCOPE (&lock);
    keep = MIN (h->maxdata, ROUND_DOWN (count, h->minblock));
    if (next->pwrite (next, buf, keep, offs, flags, err) == -1)
      return -1;
    buf += keep;
    offs += keep;
    count -= keep;
  }

  /* Unaligned tail */
  if (count) {
    ACQUIRE_WRLOCK_FOR_CURRENT_SCOPE (&lock);
    if (next->pread (next, bounce, h->minblock, offs, 0, err) == -1)
      return -1;
    memcpy (bounce, buf, count);
    if (next->pwrite (next, bounce, h->minblock, offs, flags, err) == -1)
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
  struct blocksize_handle *h = handle;
  uint32_t keep;
  bool need_flush = false;

  if ((flags & NBDKIT_FLAG_FUA) &&
      next->can_fua (next) == NBDKIT_FUA_EMULATE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }

  /* Ignore unaligned head */
  if (offs & (h->minblock - 1)) {
    keep = MIN (h->minblock - (offs & (h->minblock - 1)), count);
    offs += keep;
    count -= keep;
  }

  /* Ignore unaligned tail */
  count = ROUND_DOWN (count, h->minblock);

  /* Aligned body */
  while (count) {
    ACQUIRE_RDLOCK_FOR_CURRENT_SCOPE (&lock);
    keep = MIN (h->maxlen, count);
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
  struct blocksize_handle *h = handle;
  uint32_t keep;
  uint32_t drop;
  bool need_flush = false;

  if (flags & NBDKIT_FLAG_FAST_ZERO) {
    /* If we have to split the transaction, an ENOTSUP fast failure in
     * a later call would be unnecessarily delayed behind earlier
     * calls; it's easier to just declare that anything that can't be
     * done in one call to the plugin is not fast.
     */
    if ((offs | count) & (h->minblock - 1) || count > h->maxlen) {
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
  if (offs & (h->minblock - 1)) {
    ACQUIRE_WRLOCK_FOR_CURRENT_SCOPE (&lock);
    drop = offs & (h->minblock - 1);
    keep = MIN (h->minblock - drop, count);
    if (next->pread (next, bounce, h->minblock, offs - drop, 0, err) == -1)
      return -1;
    memset (bounce + drop, 0, keep);
    if (next->pwrite (next, bounce, h->minblock, offs - drop,
                      flags & ~NBDKIT_FLAG_MAY_TRIM, err) == -1)
      return -1;
    offs += keep;
    count -= keep;
  }

  /* Aligned body */
  while (count >= h->minblock) {
    ACQUIRE_RDLOCK_FOR_CURRENT_SCOPE (&lock);
    keep = MIN (h->maxlen, ROUND_DOWN (count, h->minblock));
    if (next->zero (next, keep, offs, flags, err) == -1)
      return -1;
    offs += keep;
    count -= keep;
  }

  /* Unaligned tail */
  if (count) {
    ACQUIRE_WRLOCK_FOR_CURRENT_SCOPE (&lock);
    if (next->pread (next, bounce, h->minblock, offs, 0, err) == -1)
      return -1;
    memset (bounce, 0, count);
    if (next->pwrite (next, bounce, h->minblock, offs,
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
  struct blocksize_handle *h = handle;
  CLEANUP_EXTENTS_FREE struct nbdkit_extents *extents2 = NULL;
  size_t i;
  struct nbdkit_extent e;

  extents2 = nbdkit_extents_new (ROUND_DOWN (offset, h->minblock),
                                 ROUND_UP (offset + count, h->minblock));
  if (extents2 == NULL) {
    *err = errno;
    return -1;
  }

  if (nbdkit_extents_aligned (next, MIN (ROUND_UP (count, h->minblock),
                                         h->maxlen),
                              ROUND_DOWN (offset, h->minblock), flags,
                              h->minblock, extents2, err) == -1)
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
  struct blocksize_handle *h = handle;
  uint32_t limit;
  uint64_t remaining = count; /* Rounding out could exceed 32 bits */

  /* Unaligned head */
  limit = offs & (h->minblock - 1);
  remaining += limit;
  offs -= limit;

  /* Unaligned tail */
  remaining = ROUND_UP (remaining, h->minblock);

  /* Aligned body */
  while (remaining) {
    limit = MIN (h->maxdata, remaining);
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
  .open              = blocksize_open,
  .prepare           = blocksize_prepare,
  .close             = free,
  .get_size          = blocksize_get_size,
  .block_size        = blocksize_block_size,
  .pread             = blocksize_pread,
  .pwrite            = blocksize_pwrite,
  .trim              = blocksize_trim,
  .zero              = blocksize_zero,
  .extents           = blocksize_extents,
  .cache             = blocksize_cache,
};

NBDKIT_REGISTER_FILTER (filter)
