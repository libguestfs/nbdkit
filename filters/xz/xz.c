/* nbdkit
 * Copyright (C) 2013-2020 Red Hat Inc.
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
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <lzma.h>

#include <nbdkit-filter.h>

#include "xzfile.h"
#include "blkcache.h"

static uint64_t maxblock = 512 * 1024 * 1024;
static uint32_t maxdepth = 8;

static int
xz_config (nbdkit_next_config *next, void *nxdata,
           const char *key, const char *value)
{
  if (strcmp (key, "xz-max-block") == 0) {
    int64_t r = nbdkit_parse_size (value);
    if (r == -1)
      return -1;
    maxblock = (uint64_t) r;
    return 0;
  }
  else if (strcmp (key, "xz-max-depth") == 0) {
    if (nbdkit_parse_uint32_t ("xz-max-depth", value, &maxdepth) == -1)
      return -1;
    if (maxdepth == 0) {
      nbdkit_error ("'xz-max-depth' parameter must be >= 1");
      return -1;
    }
    return 0;
  }
  else
    return next (nxdata, key, value);
}

#define xz_config_help \
  "xz-max-block=<SIZE> (optional) Maximum block size allowed (default: 512M)\n"\
  "xz-max-depth=<N>    (optional) Maximum blocks in cache (default: 4)\n"

/* The per-connection handle. */
struct xz_handle {
  xzfile *xz;

  /* Block cache. */
  blkcache *c;
};

/* Create the per-connection handle. */
static void *
xz_open (nbdkit_next_open *next, void *nxdata,
         int readonly, const char *exportname, int is_tls)
{
  struct xz_handle *h;

  /* Always pass readonly=1 to the underlying plugin. */
  if (next (nxdata, 1, exportname) == -1)
    return NULL;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  h->c = new_blkcache (maxdepth);
  if (!h->c) {
    free (h);
    return NULL;
  }

  /* Initialized in xz_prepare. */
  h->xz = NULL;

  return h;
}

/* Free up the per-connection handle. */
static void
xz_close (void *handle)
{
  struct xz_handle *h = handle;
  blkcache_stats stats;

  blkcache_get_stats (h->c, &stats);
  nbdkit_debug ("cache: hits = %zu, misses = %zu", stats.hits, stats.misses);

  xzfile_close (h->xz);
  free_blkcache (h->c);
  free (h);
}

static int
xz_prepare (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle,
            int readonly)
{
  struct xz_handle *h = handle;

  h->xz = xzfile_open (next_ops, nxdata);
  if (!h->xz)
    return -1;

  if (maxblock < xzfile_max_uncompressed_block_size (h->xz)) {
    nbdkit_error ("xz file largest block is bigger than maxblock\n"
                  "Either recompress the xz file with smaller blocks "
                  "(see nbdkit-xz-filter(1))\n"
                  "or make maxblock parameter bigger.\n"
                  "maxblock = %" PRIu64 " (bytes)\n"
                  "largest block in xz file = %" PRIu64 " (bytes)",
                  maxblock,
                  xzfile_max_uncompressed_block_size (h->xz));
    return -1;
  }

  return 0;
}

/* Get the file size. */
static int64_t
xz_get_size (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  struct xz_handle *h = handle;

  return xzfile_get_size (h->xz);
}

/* We need this because otherwise the layer below can_write is called
 * and that might return true (eg. if the plugin has a pwrite method
 * at all), resulting in writes being passed through to the layer
 * below.  This is possibly a bug in nbdkit.
 */
static int
xz_can_write (struct nbdkit_next_ops *next_ops, void *nxdata,
              void *handle)
{
  return 0;
}

/* Similar to above.  However xz files themselves do support
 * sparseness so in future we should generate extents information. XXX
 */
static int
xz_can_extents (struct nbdkit_next_ops *next_ops, void *nxdata,
                void *handle)
{
  return 0;
}

/* Cache */
static int
xz_can_cache (struct nbdkit_next_ops *next_ops, void *nxdata,
              void *handle)
{
  /* We are already operating as a cache regardless of the plugin's
   * underlying .can_cache, but it's easiest to just rely on nbdkit's
   * behavior of calling .pread for caching.
   */
  return NBDKIT_CACHE_EMULATE;
}

/* Read data from the file. */
static int
xz_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
          void *handle, void *buf, uint32_t count, uint64_t offset,
          uint32_t flags, int *err)
{
  struct xz_handle *h = handle;
  char *data;
  uint64_t start, size;
  uint32_t n;

  /* Find the block in the cache. */
  data = get_block (h->c, offset, &start, &size);
  if (!data) {
    /* Not in the cache.  We need to read the block from the xz file. */
    data = xzfile_read_block (h->xz, next_ops, nxdata, flags, err,
                              offset, &start, &size);
    if (data == NULL)
      return -1;
    put_block (h->c, start, size, data);
  }

  /* It's possible if the blocks are really small or oddly aligned or
   * if the requests are large that we need to read the following
   * block to satisfy the request.
   */
  n = count;
  if (start + size - offset < n)
    n = start + size - offset;

  memcpy (buf, &data[offset-start], n);
  buf += n;
  count -= n;
  offset += n;
  if (count > 0)
    return xz_pread (next_ops, nxdata, h, buf, count, offset, flags, err);

  return 0;
}

static int xz_thread_model (void)
{
  return NBDKIT_THREAD_MODEL_SERIALIZE_REQUESTS;
}

static struct nbdkit_filter filter = {
  .name              = "xz",
  .longname          = "nbdkit XZ filter",
  .config            = xz_config,
  .config_help       = xz_config_help,
  .thread_model      = xz_thread_model,
  .open              = xz_open,
  .close             = xz_close,
  .prepare           = xz_prepare,
  .get_size          = xz_get_size,
  .can_write         = xz_can_write,
  .can_extents       = xz_can_extents,
  .can_cache         = xz_can_cache,
  .pread             = xz_pread,
};

NBDKIT_REGISTER_FILTER(filter)
