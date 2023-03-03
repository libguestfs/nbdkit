/* nbdkit
 * Copyright (C) 2020 François Revol.
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
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include <nbdkit-filter.h>

#include "byte-swapping.h"
#include "isaligned.h"
#include "cleanup.h"
#include "minmax.h"
#include "rounding.h"

/* Can only be 8 (filter disabled), 16, 32 or 64. */
static int bits = 16;

/* Called for each key=value passed on the command line. */
static int
swab_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
             const char *key, const char *value)
{
  int r;

  if (strcmp (key, "swab-bits") == 0) {
    r = nbdkit_parse_int ("swab-bits", value, &bits);
    if (r == -1)
      return -1;
    if (bits != 8 && bits != 16 && bits != 32 && bits != 64) {
      nbdkit_error ("invalid swab-bits, must be 8, 16, 32 or 64");
      return -1;
    }
    return 0;
  }
  else
    return next (nxdata, key, value);
}

#define swab_config_help \
  "swab-bits=8|16|32|64       Size of byte swap (default 16)."

/* Round size down to avoid issues at end of file. */
static int64_t
swab_get_size (nbdkit_next *next,
               void *handle)
{
  int64_t size = next->get_size (next);

  if (size == -1)
    return -1;
  return ROUND_DOWN (size, bits/8);
}

/* Block size constraints. */
static int
swab_block_size (nbdkit_next *next, void *handle,
                 uint32_t *minimum, uint32_t *preferred, uint32_t *maximum)
{
  if (next->block_size (next, minimum, preferred, maximum) == -1)
    return -1;

  if (*minimum == 0) {         /* No constraints set by the plugin. */
    *minimum = bits/8;
    *preferred = 512;
    *maximum = 0xffffffff;
  }
  else {
    *minimum = MAX (*minimum, bits/8);
  }

  return 0;
}

/* The request must be aligned.
 * If you want finer alignment, use the blocksize filter.
 */
static bool
is_aligned (uint32_t count, uint64_t offset, int *err)
{
  if (!IS_ALIGNED (count, bits/8) || !IS_ALIGNED (offset, bits/8)) {
    nbdkit_error ("swab: requests to this filter must be aligned");
    *err = EINVAL;
    return false;
  }
  return true;
}

/* Byte swap, works either from one buffer to another or in-place. */
static void
buf_bswap (void *dest, const void *src, uint32_t count)
{
  uint32_t i;
  uint16_t *d16, *s16;
  uint32_t *d32, *s32;
  uint64_t *d64, *s64;

  switch (bits) {
  case 8: /* nothing */ break;
  case 16:
    d16 = (uint16_t *) dest;
    s16 = (uint16_t *) src;
    for (i = 0; i < count; i += 2)
      *d16++ = bswap_16 (*s16++);
    break;
  case 32:
    d32 = (uint32_t *) dest;
    s32 = (uint32_t *) src;
    for (i = 0; i < count; i += 4)
      *d32++ = bswap_32 (*s32++);
    break;
  case 64:
    d64 = (uint64_t *) dest;
    s64 = (uint64_t *) src;
    for (i = 0; i < count; i += 8)
      *d64++ = bswap_64 (*s64++);
    break;
  }
}

/* Read data. */
static int
swab_pread (nbdkit_next *next,
            void *handle, void *buf, uint32_t count, uint64_t offset,
            uint32_t flags, int *err)
{
  int r;

  if (!is_aligned (count, offset, err)) return -1;

  r = next->pread (next, buf, count, offset, flags, err);
  if (r == -1)
    return -1;

  /* for reads we can do it in-place */
  buf_bswap (buf, buf, count);
  return 0;
}

/* Write data. */
static int
swab_pwrite (nbdkit_next *next,
             void *handle, const void *buf, uint32_t count, uint64_t offset,
             uint32_t flags, int *err)
{
  CLEANUP_FREE uint16_t *block = NULL;

  if (!is_aligned (count, offset, err)) return -1;

  block = malloc (count);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  buf_bswap (block, buf, count);

  return next->pwrite (next, block, count, offset, flags, err);
}

/* Trim data. */
static int
swab_trim (nbdkit_next *next,
           void *handle, uint32_t count, uint64_t offset, uint32_t flags,
           int *err)
{
  if (!is_aligned (count, offset, err)) return -1;
  return next->trim (next, count, offset, flags, err);
}

/* Zero data. */
static int
swab_zero (nbdkit_next *next,
           void *handle, uint32_t count, uint64_t offset, uint32_t flags,
           int *err)
{
  if (!is_aligned (count, offset, err)) return -1;
  return next->zero (next, count, offset, flags, err);
}

/* Extents. */
static int
swab_extents (nbdkit_next *next,
              void *handle, uint32_t count, uint64_t offset, uint32_t flags,
              struct nbdkit_extents *extents, int *err)
{
  if (!is_aligned (count, offset, err)) return -1;
  return nbdkit_extents_aligned (next, count, offset, flags, bits / 8, extents,
                                 err);
}

/* Cache. */
static int
swab_cache (nbdkit_next *next,
            void *handle, uint32_t count, uint64_t offset, uint32_t flags,
            int *err)
{
  if (!is_aligned (count, offset, err)) return -1;
  return next->cache (next, count, offset, flags, err);
}


static struct nbdkit_filter filter = {
  .name              = "swab",
  .longname          = "nbdkit byte-swapping filter",
  .config            = swab_config,
  .config_help       = swab_config_help,
  .get_size          = swab_get_size,
  .block_size        = swab_block_size,
  .pread             = swab_pread,
  .pwrite            = swab_pwrite,
  .trim              = swab_trim,
  .zero              = swab_zero,
  .extents           = swab_extents,
  .cache             = swab_cache,
};

NBDKIT_REGISTER_FILTER (filter)
