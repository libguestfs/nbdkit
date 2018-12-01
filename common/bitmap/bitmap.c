/* nbdkit
 * Copyright (C) 2018 Red Hat Inc.
 * All rights reserved.
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
#include <string.h>
#include <stdint.h>

#include <nbdkit-plugin.h>

#include "bitmap.h"
#include "rounding.h"

int
bitmap_resize (struct bitmap *bm, uint64_t new_size)
{
  uint8_t *new_bitmap;
  const size_t old_bm_size = bm->size;
  uint64_t new_bm_size_u64;
  size_t new_bm_size;

  new_bm_size_u64 = DIV_ROUND_UP (new_size,
                                  bm->blksize * UINT64_C(8) / bm->bpb);
  if (new_bm_size_u64 > SIZE_MAX) {
    nbdkit_error ("bitmap too large for this architecture");
    return -1;
  }
  new_bm_size = (size_t) new_bm_size_u64;

  new_bitmap = realloc (bm->bitmap, new_bm_size);
  if (new_bitmap == NULL) {
    nbdkit_error ("realloc: %m");
    return -1;
  }
  bm->bitmap = new_bitmap;
  bm->size = new_bm_size;
  if (old_bm_size < new_bm_size)
    memset (&bm->bitmap[old_bm_size], 0, new_bm_size-old_bm_size);

  nbdkit_debug ("bitmap resized to %zu bytes", new_bm_size);

  return 0;
}
