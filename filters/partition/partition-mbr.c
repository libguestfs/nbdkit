/* nbdkit
 * Copyright (C) 2018-2019 Red Hat Inc.
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
#include <stdint.h>
#include <string.h>

#include <nbdkit-filter.h>

#include "byte-swapping.h"

#include "partition.h"

struct mbr_partition {
  uint8_t part_type_byte;
  uint32_t start_sector;
  uint32_t nr_sectors;
};

static void
get_mbr_partition (uint8_t *sector, int i, struct mbr_partition *part)
{
  int offset = 0x1BE + i*0x10;

  part->part_type_byte = sector[offset+4];
  memcpy (&part->start_sector, &sector[offset+8], 4);
  part->start_sector = le32toh (part->start_sector);
  memcpy (&part->nr_sectors, &sector[offset+0xC], 4);
  part->nr_sectors = le32toh (part->nr_sectors);
}

int
find_mbr_partition (struct nbdkit_next_ops *next_ops, void *nxdata,
                    int64_t size, uint8_t *mbr,
                    int64_t *offset_r, int64_t *range_r)
{
  int i;
  struct mbr_partition partition;

  if (partnum > 4) {
    nbdkit_error ("MBR logical partitions are not supported");
    return -1;
  }

  for (i = 0; i < 4; ++i) {
    get_mbr_partition (mbr, i, &partition);
    if (partition.nr_sectors > 0 &&
        partition.part_type_byte != 0 &&
        partnum == i+1) {
      *offset_r = partition.start_sector * 512;
      *range_r = partition.nr_sectors * 512;
      return 0;
    }
  }

  nbdkit_error ("MBR partition %d not found", partnum);
  return -1;
}
