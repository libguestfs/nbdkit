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

struct gpt_header {
  uint32_t nr_partitions;
  uint32_t partition_entry_size;
};

static void
get_gpt_header (uint8_t *sector, struct gpt_header *header)
{
  memcpy (&header->nr_partitions, &sector[0x50], 4);
  header->nr_partitions = le32toh (header->nr_partitions);
  memcpy (&header->partition_entry_size, &sector[0x54], 4);
  header->partition_entry_size = le32toh (header->partition_entry_size);
}

struct gpt_partition {
  uint8_t partition_type_guid[16];
  uint64_t first_lba;
  uint64_t last_lba;
};

static void
get_gpt_partition (uint8_t *bytes, struct gpt_partition *part)
{
  memcpy (&part->partition_type_guid, &bytes[0], 16);
  memcpy (&part->first_lba, &bytes[0x20], 8);
  part->first_lba = le64toh (part->first_lba);
  memcpy (&part->last_lba, &bytes[0x28], 8);
  part->last_lba = le64toh (part->last_lba);
}

int
find_gpt_partition (struct nbdkit_next_ops *next_ops, void *nxdata,
                    int64_t size, uint8_t *header_bytes,
                    int64_t *offset_r, int64_t *range_r)
{
  uint8_t partition_bytes[128];
  struct gpt_header header;
  struct gpt_partition partition;
  int i;
  int err;

  get_gpt_header (header_bytes, &header);
  if (partnum > header.nr_partitions) {
    nbdkit_error ("GPT partition number out of range");
    return -1;
  }

  if (header.partition_entry_size < 128) {
    nbdkit_error ("GPT partition entry size is < 128 bytes");
    return -1;
  }

  /* Check the disk is large enough to contain the partition table
   * array (twice) plus other GPT overheads.  Otherwise it is likely
   * that the GPT header is bogus.
   */
  if (size < INT64_C(3)*SECTOR_SIZE +
      INT64_C(2) * header.nr_partitions * header.partition_entry_size) {
    nbdkit_error ("GPT partition table is too large for this disk");
    return -1;
  }

  for (i = 0; i < header.nr_partitions; ++i) {
    /* We already checked these are within bounds above. */
    if (next_ops->pread (nxdata, partition_bytes, sizeof partition_bytes,
                         2*SECTOR_SIZE + i*header.partition_entry_size, 0,
                         &err) == -1)
      return -1;
    get_gpt_partition (partition_bytes, &partition);
    if (memcmp (partition.partition_type_guid,
                "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) != 0 &&
        partnum == i+1) {
      *offset_r = partition.first_lba * SECTOR_SIZE;
      *range_r = (1 + partition.last_lba - partition.first_lba) * SECTOR_SIZE;
      return 0;
    }
  }

  nbdkit_error ("GPT partition %d not found", partnum);
  return -1;
}
