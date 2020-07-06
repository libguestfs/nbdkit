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

/* The request must be aligned.
 * XXX We could lift this restriction with more work.
 */
static bool
is_aligned (uint32_t count, uint64_t offset)
{
  if (!IS_ALIGNED (count, 2) || !IS_ALIGNED (offset, 2)) {
    nbdkit_error ("swab: requests to this filter must be aligned");
    return false;
  }
  return true;
}

/* Read data. */
static int
swab_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle, void *buf, uint32_t count, uint64_t offset,
            uint32_t flags, int *err)
{
  size_t i;
  uint16_t *p;
  int r;

  if (!is_aligned (count, offset)) return -1;

  r = next_ops->pread (nxdata, buf, count, offset, flags, err);
  if (r == -1)
    return -1;

  /* for reads we can do it in-place */
  for (i = 0, p = (uint16_t *)buf; i < count; i += 2, p++)
    *p = bswap_16 (*p);

  return r;
}

/* Write data. */
static int
swab_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle, const void *buf, uint32_t count, uint64_t offset,
             uint32_t flags, int *err)
{
  CLEANUP_FREE uint16_t *block = NULL;
  size_t i;
  uint16_t *p = (uint16_t *)buf;
  uint16_t *q;

  if (!is_aligned (count, offset)) return -1;

  block = malloc (count);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  for (i = 0, q = block; i < count; i += 2)
    *q++ = bswap_16 (*p++);

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
  .pread             = swab_pread,
  .pwrite            = swab_pwrite,
  .trim              = swab_trim,
  .zero              = swab_zero,
  .can_extents       = swab_can_extents,
  .cache             = swab_cache,
};

NBDKIT_REGISTER_FILTER(filter)
