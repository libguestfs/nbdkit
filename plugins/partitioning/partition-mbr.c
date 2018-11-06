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
#include <inttypes.h>
#include <assert.h>

#include <nbdkit-plugin.h>

#include "byte-swapping.h"

#include "isaligned.h"
#include "rounding.h"

#include "regions.h"
#include "virtual-disk.h"

/* Create the partition table. */
void
create_mbr_partition_table (unsigned char *out)
{
  size_t i, j;

  for (j = 0; j < nr_regions (&regions); ++j) {
    const struct region *region = get_region (&regions, j);

    if (region->type == region_file) {
      i = region->u.i;
      assert (i < 4);
      create_mbr_partition_table_entry (region, i == 0,
                                        files[i].mbr_id,
                                        &out[0x1be + 16*i]);
    }
  }

  /* Boot signature. */
  out[0x1fe] = 0x55;
  out[0x1ff] = 0xaa;
}

static void
chs_too_large (unsigned char *out)
{
  const int c = 1023, h = 254, s = 63;

  out[0] = h;
  out[1] = (c & 0x300) >> 2 | s;
  out[2] = c & 0xff;
}

void
create_mbr_partition_table_entry (const struct region *region,
                                  int bootable, int partition_id,
                                  unsigned char *out)
{
  uint64_t start_sector, nr_sectors;
  uint32_t u32;

  assert (IS_ALIGNED (region->start, SECTOR_SIZE));

  start_sector = region->start / SECTOR_SIZE;
  nr_sectors = DIV_ROUND_UP (region->len, SECTOR_SIZE);

  /* The total_size test in partitioning_config_complete should catch
   * this earlier.
   */
  assert (start_sector <= UINT32_MAX);
  assert (nr_sectors <= UINT32_MAX);

  out[0] = bootable ? 0x80 : 0;
  chs_too_large (&out[1]);
  out[4] = partition_id;
  chs_too_large (&out[5]);
  u32 = htole32 (start_sector);
  memcpy (&out[8], &u32, 4);
  u32 = htole32 (nr_sectors);
  memcpy (&out[12], &u32, 4);
}
