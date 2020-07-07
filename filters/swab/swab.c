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
#include "rounding.h"

/* Can only be 8 (filter disabled), 16, 32 or 64. */
static int bits = 16;

/* Called for each key=value passed on the command line. */
static int
swab_config (nbdkit_next_config *next, void *nxdata,
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
swab_get_size (struct nbdkit_next_ops *next_ops, void *nxdata,
               void *handle)
{
  int64_t size = next_ops->get_size (nxdata);

  if (size == -1)
    return -1;
  return ROUND_DOWN (size, bits/8);
}

/* The request must be aligned.
 * XXX We could lift this restriction with more work.
 */
static bool
is_aligned (uint32_t count, uint64_t offset)
{
  if (!IS_ALIGNED (count, bits/8) || !IS_ALIGNED (offset, bits/8)) {
    nbdkit_error ("swab: requests to this filter must be aligned");
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
swab_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle, void *buf, uint32_t count, uint64_t offset,
            uint32_t flags, int *err)
{
  int r;

  if (!is_aligned (count, offset)) return -1;

  r = next_ops->pread (nxdata, buf, count, offset, flags, err);
  if (r == -1)
    return -1;

  /* for reads we can do it in-place */
  buf_bswap (buf, buf, count);
  return 0;
}

/* Write data. */
static int
swab_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle, const void *buf, uint32_t count, uint64_t offset,
             uint32_t flags, int *err)
{
  CLEANUP_FREE uint16_t *block = NULL;

  if (!is_aligned (count, offset)) return -1;

  block = malloc (count);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  buf_bswap (block, buf, count);

  return next_ops->pwrite (nxdata, block, count, offset, flags, err);
}

/* Trim data. */
static int
swab_trim (struct nbdkit_next_ops *next_ops, void *nxdata,
           void *handle, uint32_t count, uint64_t offset, uint32_t flags,
           int *err)
{
  if (!is_aligned (count, offset)) return -1;
  return next_ops->trim (nxdata, count, offset, flags, err);
}

/* Zero data. */
static int
swab_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
           void *handle, uint32_t count, uint64_t offset, uint32_t flags,
           int *err)
{
  if (!is_aligned (count, offset)) return -1;
  return next_ops->zero (nxdata, count, offset, flags, err);
}

/* FIXME: Extents could be useful, but if the underlying plugin ever reports
 * values not aligned to 2 bytes, it is complicated to adjust that correctly.
 * In the short term, we punt by disabling extents.
 */
static int
swab_can_extents (struct nbdkit_next_ops *next_ops, void *nxdata,
                  void *handle)
{
  return 0;
}

/* Cache. */
static int
swab_cache (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle, uint32_t count, uint64_t offset, uint32_t flags,
            int *err)
{
  if (!is_aligned (count, offset)) return -1;
  return next_ops->cache (nxdata, count, offset, flags, err);
}


static struct nbdkit_filter filter = {
  .name              = "swab",
  .longname          = "nbdkit byte-swapping filter",
  .config            = swab_config,
  .config_help       = swab_config_help,
  .get_size          = swab_get_size,
  .pread             = swab_pread,
  .pwrite            = swab_pwrite,
  .trim              = swab_trim,
  .zero              = swab_zero,
  .can_extents       = swab_can_extents,
  .cache             = swab_cache,
};

NBDKIT_REGISTER_FILTER(filter)
