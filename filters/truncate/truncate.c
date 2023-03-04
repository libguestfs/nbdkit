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
#include <limits.h>
#include <errno.h>
#include <inttypes.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "ispowerof2.h"
#include "iszero.h"
#include "rounding.h"

/* These are the parameters. */
static int64_t truncate_size = -1;
static unsigned round_up = 0, round_down = 0;

static int
parse_round_param (const char *key, const char *value, unsigned *ret)
{
  int64_t r;
  unsigned u;

  /* Parse it as a "size" quantity so we allow round-up=1M and similar. */
  r = nbdkit_parse_size (value);
  if (r == -1)
    return -1;

  /* Must not be zero or larger than an unsigned int. */
  if (r == 0) {
    nbdkit_error ("if set, the %s parameter must be > 0", key);
    return -1;
  }
  if (r > UINT_MAX) {
    nbdkit_error ("the %s parameter is too large", key);
    return -1;
  }
  u = r;

  /* Must be a power of 2.  We could relax this in future. */
  if (!is_power_of_2 (u)) {
    nbdkit_error ("the %s parameter must be a power of 2", key);
    return -1;
  }

  *ret = u;
  return 0;
}

/* Called for each key=value passed on the command line. */
static int
truncate_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
                 const char *key, const char *value)
{
  if (strcmp (key, "truncate") == 0) {
    truncate_size = nbdkit_parse_size (value);
    if (truncate_size == -1)
      return -1;
    return 0;
  }
  else if (strcmp (key, "round-up") == 0) {
    return parse_round_param (key, value, &round_up);
  }
  else if (strcmp (key, "round-down") == 0) {
    return parse_round_param (key, value, &round_down);
  }
  else
    return next (nxdata, key, value);
}

#define truncate_config_help \
  "truncate=<SIZE>                The new size.\n" \
  "round-up=<N>                   Round up to next multiple of N.\n" \
  "round-down=<N>                 Round down to multiple of N."

/* Per-connection state. Until the NBD protocol gains dynamic resize
 * support, each connection remembers the size of the underlying
 * plugin at open (even if that size differs between connections
 * because the plugin tracks external resize effects).
 */
struct handle {
  /* The real size of the underlying plugin. */
  uint64_t real_size;

  /* The calculated size after applying the parameters. */
  uint64_t size;
};

/* Open a connection. */
static void *
truncate_open (nbdkit_next_open *next, nbdkit_context *nxdata,
               int readonly, const char *exportname, int is_tls)
{
  struct handle *h;

  if (next (nxdata, readonly, exportname) == -1)
    return NULL;

  h = malloc (sizeof *h); /* h is populated during .prepare */
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  return h;
}

static void
truncate_close (void *handle)
{
  struct handle *h = handle;

  free (h);
}

/* In prepare, force a call to next->get_size in order to set
 * per-connection real_size & size; these values are not changed
 * during the life of the connection.
 */
static int
truncate_prepare (nbdkit_next *next,
                  void *handle, int readonly)
{
  int64_t r;
  struct handle *h = handle;

  r = next->get_size (next);
  if (r == -1)
    return -1;

  h->real_size = h->size = r;

  /* The truncate, round-up and round-down parameters are treated as
   * separate operations.  It's possible to specify more than one,
   * although perhaps not very useful.
   */
  if (truncate_size >= 0)
    h->size = truncate_size;
  if (round_up > 0) {
    if (ROUND_UP (h->size, round_up) > INT64_MAX) {
      nbdkit_error ("cannot round size %" PRId64 " up to next boundary of %u",
                    h->size, round_up);
      return -1;
    }
    h->size = ROUND_UP (h->size, round_up);
  }
  if (round_down > 0)
    h->size = ROUND_DOWN (h->size, round_down);

  return r >= 0 ? 0 : -1;
}

/* Get the size. */
static int64_t
truncate_get_size (nbdkit_next *next,
                   void *handle)
{
  struct handle *h = handle;

  /* If the NBD protocol and nbdkit adds dynamic resize, we'll need a
   * rwlock where get_size holds write lock and all other ops hold
   * read lock. Until then, NBD sizes are unchanging (even if the
   * underlying plugin can react to external size changes), so just
   * returned what we cached at connection open.
   */
  return h->size;
}

/* Advertise extents support. */
static int
truncate_can_extents (nbdkit_next *next,
                      void *handle)
{
  /* Advertise unconditional support for the image tail, but also call
   * into next to ensure next->extents doesn't fail later.
   */
  int r = next->can_extents (next);
  if (r == -1)
    return -1;
  return 1;
}

/* Override the plugin's .can_fast_zero, because zeroing a tail is fast. */
static int
truncate_can_fast_zero (nbdkit_next *next,
                        void *handle)
{
  /* Cache next->can_fast_zero now, so later calls don't fail,
   * even though we override the answer here.
   */
  int r = next->can_fast_zero (next);
  if (r == -1)
    return -1;
  return 1;
}

/* Read data. */
static int
truncate_pread (nbdkit_next *next,
                void *handle, void *buf, uint32_t count, uint64_t offset,
                uint32_t flags, int *err)
{
  int r;
  uint32_t n;
  struct handle *h = handle;

  if (offset < h->real_size) {
    if (offset + count <= h->real_size)
      n = count;
    else
      n = h->real_size - offset;
    r = next->pread (next, buf, n, offset, flags, err);
    if (r == -1)
      return -1;
    count -= n;
    buf += n;
  }

  if (count > 0)
    memset (buf, 0, count);

  return 0;
}

/* Write data. */
static int
truncate_pwrite (nbdkit_next *next,
                 void *handle,
                 const void *buf, uint32_t count, uint64_t offset,
                 uint32_t flags, int *err)
{
  int r;
  uint32_t n;
  struct handle *h = handle;

  if (offset < h->real_size) {
    if (offset + count <= h->real_size)
      n = count;
    else
      n = h->real_size - offset;
    r = next->pwrite (next, buf, n, offset, flags, err);
    if (r == -1)
      return -1;
    count -= n;
    buf += n;
  }

  if (count > 0) {
    /* The caller must be writing zeroes, else it's an error. */
    if (!is_zero (buf, count)) {
      nbdkit_error ("truncate: write beyond end of underlying device");
      *err = ENOSPC;
      return -1;
    }
  }

  return 0;
}

/* Trim data. */
static int
truncate_trim (nbdkit_next *next,
               void *handle, uint32_t count, uint64_t offset,
               uint32_t flags, int *err)
{
  uint32_t n;
  struct handle *h = handle;

  if (offset < h->real_size) {
    if (offset + count <= h->real_size)
      n = count;
    else
      n = h->real_size - offset;
    return next->trim (next, n, offset, flags, err);
  }
  return 0;
}

/* Zero data. */
static int
truncate_zero (nbdkit_next *next,
               void *handle, uint32_t count, uint64_t offset,
               uint32_t flags, int *err)
{
  uint32_t n;
  struct handle *h = handle;

  if (offset < h->real_size) {
    if (offset + count <= h->real_size)
      n = count;
    else
      n = h->real_size - offset;
    if (flags & NBDKIT_FLAG_FAST_ZERO &&
        next->can_fast_zero (next) == 0) {
      *err = ENOTSUP;
      return -1;
    }
    return next->zero (next, n, offset, flags, err);
  }
  return 0;
}

/* Extents. */
static int
truncate_extents (nbdkit_next *next,
                  void *handle, uint32_t count, uint64_t offset,
                  uint32_t flags, struct nbdkit_extents *extents, int *err)
{
  uint32_t n;
  struct handle *h = handle;
  CLEANUP_EXTENTS_FREE struct nbdkit_extents *extents2 = NULL;
  size_t i;

  /* If the entire request is beyond the end of the underlying plugin
   * then this is the easy case: return a hole up to the end of the
   * file.
   */
  if (offset >= h->real_size) {
    int r = nbdkit_add_extent (extents,
                               h->real_size, truncate_size - h->real_size,
                               NBDKIT_EXTENT_ZERO|NBDKIT_EXTENT_HOLE);
    if (r == -1)
      *err = errno;
    return r;
  }

  /* We're asked first for extents information about the plugin, then
   * possibly (if truncating larger) for the hole after the plugin.
   * Since we're not required to provide all of this information, the
   * easiest thing is to only return data from the plugin.  We will be
   * called later about the hole.  However we do need to make sure
   * that the extents array is truncated to the real size, hence we
   * have to create a new extents array, ask the plugin, then copy the
   * returned data to the original array.
   */
  extents2 = nbdkit_extents_new (offset, h->real_size);
  if (extents2 == NULL) {
    *err = errno;
    return -1;
  }
  if (offset + count <= h->real_size)
    n = count;
  else
    n = h->real_size - offset;
  if (next->extents (next, n, offset, flags, extents2, err) == -1)
    return -1;

  for (i = 0; i < nbdkit_extents_count (extents2); ++i) {
    struct nbdkit_extent e = nbdkit_get_extent (extents2, i);

    if (nbdkit_add_extent (extents, e.offset, e.length, e.type) == -1) {
      *err = errno;
      return -1;
    }
  }

  return 0;
}

/* Cache. */
static int
truncate_cache (nbdkit_next *next,
                void *handle, uint32_t count, uint64_t offset,
                uint32_t flags, int *err)
{
  int r;
  uint32_t n;
  struct handle *h = handle;

  if (offset < h->real_size) {
    if (offset + count <= h->real_size)
      n = count;
    else
      n = h->real_size - offset;
    r = next->cache (next, n, offset, flags, err);
    if (r == -1)
      return -1;
  }

  return 0;
}

static struct nbdkit_filter filter = {
  .name              = "truncate",
  .longname          = "nbdkit truncate filter",
  .config            = truncate_config,
  .config_help       = truncate_config_help,
  .can_extents       = truncate_can_extents,
  .open              = truncate_open,
  .close             = truncate_close,
  .prepare           = truncate_prepare,
  .get_size          = truncate_get_size,
  .can_fast_zero     = truncate_can_fast_zero,
  .pread             = truncate_pread,
  .pwrite            = truncate_pwrite,
  .trim              = truncate_trim,
  .zero              = truncate_zero,
  .extents           = truncate_extents,
  .cache             = truncate_cache,
};

NBDKIT_REGISTER_FILTER (filter)
