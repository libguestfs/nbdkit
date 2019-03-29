/* nbdkit
 * Copyright (C) 2018 Red Hat Inc.
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

#include <nbdkit-filter.h>

#include "minmax.h"
#include "rounding.h"

/* XXX See design comment in filters/cow/cow.c. */
#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

#define BLOCKSIZE_MIN_LIMIT (64U * 1024)

/* As long as we don't have parallel requests, we can reuse a common
 * buffer for alignment purposes; size it to the maximum we allow for
 * minblock. */
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
blocksize_config (nbdkit_next_config *next, void *nxdata,
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
blocksize_config_complete (nbdkit_next_config_complete *next, void *nxdata)
{
  if (minblock) {
    if (minblock & (minblock - 1)) {
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

static int
blocksize_prepare (struct nbdkit_next_ops *next_ops, void *nxdata,
                   void *handle)
{
  /* Early call to get_size to ensure it doesn't truncate to 0. */
  int64_t size = next_ops->get_size (nxdata);

  if (size == -1)
    return -1;
  if (size < minblock) {
    nbdkit_error ("disk is too small for minblock size %u", minblock);
    return -1;
  }
  /* TODO: cache per-connection FUA mode? */
  return 0;
}

static int64_t
blocksize_get_size (struct nbdkit_next_ops *next_ops, void *nxdata,
                    void *handle)
{
  int64_t size = next_ops->get_size (nxdata);

  return size == -1 ? size : size & ~(minblock - 1);
}

static int
blocksize_can_multi_conn (struct nbdkit_next_ops *next_ops, void *nxdata,
                          void *handle)
{
  /* Although we are serializing all requests anyway so this likely
   * doesn't make a difference, return false because the bounce buffer
   * is not consistent for flush.
   */
  return 0;
}

static int
blocksize_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
                 void *handle, void *b, uint32_t count, uint64_t offs,
                 uint32_t flags, int *err)
{
  char *buf = b;
  uint32_t keep;
  uint32_t drop;

  /* Unaligned head */
  if (offs & (minblock - 1)) {
    drop = offs & (minblock - 1);
    keep = MIN (minblock - drop, count);
    if (next_ops->pread (nxdata, bounce, minblock, offs - drop, flags,
                         err) == -1)
      return -1;
    memcpy (buf, bounce + drop, keep);
    buf += keep;
    offs += keep;
    count -= keep;
  }

  /* Unaligned tail */
  if (count & (minblock - 1)) {
    keep = count & (minblock - 1);
    count -= keep;
    if (next_ops->pread (nxdata, bounce, minblock, offs + count, flags,
                         err) == -1)
      return -1;
    memcpy (buf + count, bounce, keep);
  }

  /* Aligned body */
  while (count) {
    keep = MIN (maxdata, count);
    if (next_ops->pread (nxdata, buf, keep, offs, flags, err) == -1)
      return -1;
    buf += keep;
    offs += keep;
    count -= keep;
  }

  return 0;
}

static int
blocksize_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
                  void *handle, const void *b, uint32_t count, uint64_t offs,
                  uint32_t flags, int *err)
{
  const char *buf = b;
  uint32_t keep;
  uint32_t drop;
  bool need_flush = false;

  if ((flags & NBDKIT_FLAG_FUA) &&
      next_ops->can_fua (nxdata) == NBDKIT_FUA_EMULATE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }

  /* Unaligned head */
  if (offs & (minblock - 1)) {
    drop = offs & (minblock - 1);
    keep = MIN (minblock - drop, count);
    if (next_ops->pread (nxdata, bounce, minblock, offs - drop, 0, err) == -1)
      return -1;
    memcpy (bounce + drop, buf, keep);
    if (next_ops->pwrite (nxdata, bounce, minblock, offs - drop, flags,
                          err) == -1)
      return -1;
    buf += keep;
    offs += keep;
    count -= keep;
  }

  /* Unaligned tail */
  if (count & (minblock - 1)) {
    keep = count & (minblock - 1);
    count -= keep;
    if (next_ops->pread (nxdata, bounce, minblock, offs + count, 0, err) == -1)
      return -1;
    memcpy (bounce, buf + count, keep);
    if (next_ops->pwrite (nxdata, bounce, minblock, offs + count, flags,
                          err) == -1)
      return -1;
  }

  /* Aligned body */
  while (count) {
    keep = MIN (maxdata, count);
    if (next_ops->pwrite (nxdata, buf, keep, offs, flags, err) == -1)
      return -1;
    buf += keep;
    offs += keep;
    count -= keep;
  }

  if (need_flush)
    return next_ops->flush (nxdata, 0, err);
  return 0;
}

static int
blocksize_trim (struct nbdkit_next_ops *next_ops, void *nxdata,
                void *handle, uint32_t count, uint64_t offs, uint32_t flags,
                int *err)
{
  uint32_t keep;
  bool need_flush = false;

  if ((flags & NBDKIT_FLAG_FUA) &&
      next_ops->can_fua (nxdata) == NBDKIT_FUA_EMULATE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }

  /* Unaligned head */
  if (offs & (minblock - 1)) {
    keep = MIN (minblock - (offs & (minblock - 1)), count);
    offs += keep;
    count -= keep;
  }

  /* Unaligned tail */
  if (count & (minblock - 1))
    count -= count & (minblock - 1);

  /* Aligned body */
  while (count) {
    keep = MIN (maxlen, count);
    if (next_ops->trim (nxdata, keep, offs, flags, err) == -1)
      return -1;
    offs += keep;
    count -= keep;
  }

  if (need_flush)
    return next_ops->flush (nxdata, 0, err);
  return 0;
}

static int
blocksize_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
                void *handle, uint32_t count, uint64_t offs, uint32_t flags,
                int *err)
{
  uint32_t keep;
  uint32_t drop;
  bool need_flush = false;

  if ((flags & NBDKIT_FLAG_FUA) &&
      next_ops->can_fua (nxdata) == NBDKIT_FUA_EMULATE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }

  /* Unaligned head */
  if (offs & (minblock - 1)) {
    drop = offs & (minblock - 1);
    keep = MIN (minblock - drop, count);
    if (next_ops->pread (nxdata, bounce, minblock, offs - drop, 0, err) == -1)
      return -1;
    memset (bounce + drop, 0, keep);
    if (next_ops->pwrite (nxdata, bounce, minblock, offs - drop,
                          flags & ~NBDKIT_FLAG_MAY_TRIM, err) == -1)
      return -1;
    offs += keep;
    count -= keep;
  }

  /* Unaligned tail */
  if (count & (minblock - 1)) {
    keep = count & (minblock - 1);
    count -= keep;
    if (next_ops->pread (nxdata, bounce, minblock, offs + count, 0, err) == -1)
      return -1;
    memset (bounce, 0, keep);
    if (next_ops->pwrite (nxdata, bounce, minblock, offs + count,
                          flags & ~NBDKIT_FLAG_MAY_TRIM, err) == -1)
      return -1;
  }

  /* Aligned body */
  while (count) {
    keep = MIN (maxlen, count);
    if (next_ops->zero (nxdata, keep, offs, flags, err) == -1)
      return -1;
    offs += keep;
    count -= keep;
  }

  if (need_flush)
    return next_ops->flush (nxdata, 0, err);
  return 0;
}

static int
blocksize_extents (struct nbdkit_next_ops *next_ops, void *nxdata,
                   void *handle, uint32_t count, uint64_t offset,
                   uint32_t flags, struct nbdkit_extents *extents, int *err)
{
  /* Ask the plugin for blocksize-aligned data.  Since the extents
   * list start is set to the real offset, everything before the
   * offset is ignored automatically.  Also we only need to ask for
   * maxlen of data, because it's fine to return less than the full
   * count as long as we're making progress.
   */
  return next_ops->extents (nxdata,
                            MIN (count, maxlen),
                            ROUND_DOWN (offset, minblock),
                            flags, extents, err);
}

static struct nbdkit_filter filter = {
  .name              = "blocksize",
  .longname          = "nbdkit blocksize filter",
  .version           = PACKAGE_VERSION,
  .config            = blocksize_config,
  .config_complete   = blocksize_config_complete,
  .config_help       = blocksize_config_help,
  .prepare           = blocksize_prepare,
  .get_size          = blocksize_get_size,
  .can_multi_conn    = blocksize_can_multi_conn,
  .pread             = blocksize_pread,
  .pwrite            = blocksize_pwrite,
  .trim              = blocksize_trim,
  .zero              = blocksize_zero,
  .extents           = blocksize_extents,
};

NBDKIT_REGISTER_FILTER(filter)
