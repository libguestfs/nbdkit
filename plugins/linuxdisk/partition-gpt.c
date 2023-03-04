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
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include <nbdkit-plugin.h>

#include "byte-swapping.h"
#include "efi-crc32.h"
#include "gpt.h"
#include "isaligned.h"
#include "rounding.h"
#include "regions.h"

#include "virtual-disk.h"

#define PARTITION_TYPE_GUID "0FC63DAF-8483-4772-8E79-3D69D8477DE4"

static void create_gpt_protective_mbr (struct virtual_disk *disk,
                                       unsigned char *out);
static void create_gpt_partition_header (struct virtual_disk *disk,
                                         const void *pt, bool is_primary,
                                         unsigned char *out);
static void create_gpt_partition_table (struct virtual_disk *disk,
                                        unsigned char *out);

/* Initialize the partition table structures. */
int
create_partition_table (struct virtual_disk *disk)
{
  create_gpt_protective_mbr (disk, disk->protective_mbr);

  create_gpt_partition_table (disk, disk->pt);

  create_gpt_partition_header (disk, disk->pt, true, disk->primary_header);
  create_gpt_partition_header (disk, disk->pt, false, disk->secondary_header);

  return 0;
}

static void
chs_too_large (unsigned char *out)
{
  const int c = 1023, h = 254, s = 63;

  out[0] = h;
  out[1] = (c & 0x300) >> 2 | s;
  out[2] = c & 0xff;
}

static void
create_mbr_partition_table_entry (const struct region *region,
                                  bool bootable, int partition_id,
                                  unsigned char *out)
{
  uint64_t start_sector, nr_sectors;
  uint32_t u32;

  assert (IS_ALIGNED (region->start, SECTOR_SIZE));

  start_sector = region->start / SECTOR_SIZE;
  nr_sectors = DIV_ROUND_UP (region->len, SECTOR_SIZE);

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

static void
create_gpt_protective_mbr (struct virtual_disk *disk, unsigned char *out)
{
  struct region region;
  uint64_t end;

  /* Protective MBR creates an MBR partition with partition ID 0xee
   * which covers the whole of the disk, or as much of the disk as
   * expressible with MBR.
   */
  region.start = 512;
  end = virtual_size (&disk->regions) - 1;
  if (end > UINT32_MAX * SECTOR_SIZE)
    end = UINT32_MAX * SECTOR_SIZE;
  region.end = end;
  region.len = region.end - region.start + 1;

  create_mbr_partition_table_entry (&region, false, 0xee, &out[0x1be]);

  /* Boot sector signature. */
  out[0x1fe] = 0x55;
  out[0x1ff] = 0xaa;
}

static void
create_gpt_partition_header (struct virtual_disk *disk,
                             const void *pt, bool is_primary,
                             unsigned char *out)
{
  uint64_t nr_lbas;
  struct gpt_header *header = (struct gpt_header *) out;

  nr_lbas = virtual_size (&disk->regions) / SECTOR_SIZE;

  memset (header, 0, sizeof *header);
  memcpy (header->signature, GPT_SIGNATURE, sizeof (header->signature));
  memcpy (header->revision, GPT_REVISION, sizeof (header->revision));
  header->header_size = htole32 (sizeof *header);
  if (is_primary) {
    header->current_lba = htole64 (1);
    header->backup_lba = htole64 (nr_lbas - 1);
  }
  else {
    header->current_lba = htole64 (nr_lbas - 1);
    header->backup_lba = htole64 (1);
  }
  header->first_usable_lba = htole64 (34);
  header->last_usable_lba = htole64 (nr_lbas - 34);
  if (is_primary)
    header->partition_entries_lba = htole64 (2);
  else
    header->partition_entries_lba = htole64 (nr_lbas - 33);
  header->nr_partition_entries = htole32 (GPT_MIN_PARTITIONS);
  header->size_partition_entry = htole32 (GPT_PT_ENTRY_SIZE);
  header->crc_partitions =
    htole32 (efi_crc32 (pt, GPT_PT_ENTRY_SIZE * GPT_MIN_PARTITIONS));

  /* Must be computed last. */
  header->crc = htole32 (efi_crc32 (header, sizeof *header));
}

static void
create_gpt_partition_table_entry (const struct region *region,
                                  bool bootable,
                                  char partition_type_guid[16],
                                  char guid[16],
                                  unsigned char *out)
{
  struct gpt_entry *entry = (struct gpt_entry *) out;

  assert (sizeof (struct gpt_entry) == GPT_PT_ENTRY_SIZE);

  memcpy (entry->partition_type_guid, partition_type_guid, 16);
  memcpy (entry->unique_guid, guid, 16);

  entry->first_lba = htole64 (region->start / SECTOR_SIZE);
  entry->last_lba = htole64 (region->end / SECTOR_SIZE);
  entry->attributes = htole64 (bootable ? 4 : 0);
}

static void
create_gpt_partition_table (struct virtual_disk *disk, unsigned char *out)
{
  size_t j;

  for (j = 0; j < nr_regions (&disk->regions); ++j) {
    const struct region *region = &disk->regions.ptr[j];

    /* Find the (only) partition region, which has type region_file. */
    if (region->type == region_file) {
      create_gpt_partition_table_entry (region, true,
                                        PARTITION_TYPE_GUID,
                                        disk->guid,
                                        out);
      out += GPT_PT_ENTRY_SIZE;
    }
  }
}
