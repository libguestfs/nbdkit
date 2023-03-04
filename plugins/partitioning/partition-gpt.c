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
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include <nbdkit-plugin.h>

#include "ascii-ctype.h"
#include "byte-swapping.h"
#include "hexdigit.h"

#include "efi-crc32.h"
#include "gpt.h"
#include "regions.h"
#include "virtual-disk.h"

static void create_gpt_partition_header (const void *pt, bool is_primary,
                                         unsigned char *out);
static void create_gpt_partition_table (unsigned char *out);
static void create_gpt_partition_table_entry (const struct region *region,
                                              bool bootable,
                                              char partition_type_guid[16],
                                              unsigned char *out);
static void create_gpt_protective_mbr (unsigned char *out);

void
create_gpt_layout (void)
{
  void *pt;

  /* Protective MBR.  LBA 0 */
  create_gpt_protective_mbr (primary);

  /* Primary partition table.  LBA 2-(LBAs+1) */
  pt = &primary[2*SECTOR_SIZE];
  create_gpt_partition_table (pt);

  /* Partition table header.  LBA 1 */
  create_gpt_partition_header (pt, true, &primary[SECTOR_SIZE]);

  /* Backup partition table.  LBA -(LBAs+2) */
  pt = secondary;
  create_gpt_partition_table (pt);

  /* Backup partition table header.  LBA -1 */
  create_gpt_partition_header (pt, false, &secondary[GPT_PTA_LBAs*SECTOR_SIZE]);
}

static void
create_gpt_partition_header (const void *pt, bool is_primary,
                             unsigned char *out)
{
  uint64_t nr_lbas;
  struct gpt_header *header = (struct gpt_header *) out;

  nr_lbas = virtual_size (&the_regions) / SECTOR_SIZE;

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
  header->first_usable_lba = htole64 (2 + GPT_PTA_LBAs);
  header->last_usable_lba = htole64 (nr_lbas - GPT_PTA_LBAs - 2);
  if (is_primary)
    header->partition_entries_lba = htole64 (2);
  else
    header->partition_entries_lba = htole64 (nr_lbas - GPT_PTA_LBAs - 1);
  header->nr_partition_entries = htole32 (GPT_PTA_SIZE);
  header->size_partition_entry = htole32 (GPT_PT_ENTRY_SIZE);
  header->crc_partitions =
    htole32 (efi_crc32 (pt, GPT_PT_ENTRY_SIZE * GPT_PTA_SIZE));

  /* Must be computed last. */
  header->crc = htole32 (efi_crc32 (header, sizeof *header));
}

static void
create_gpt_partition_table (unsigned char *out)
{
  size_t i, j;

  for (j = 0; j < nr_regions (&the_regions); ++j) {
    const struct region *region = &the_regions.ptr[j];

    if (region->type == region_file) {
      i = region->u.i;
      assert (i < GPT_PTA_SIZE);
      create_gpt_partition_table_entry (region, i == 0,
                                        the_files.ptr[i].type_guid,
                                        out);
      out += GPT_PT_ENTRY_SIZE;
    }
  }
}

static void
create_gpt_partition_table_entry (const struct region *region,
                                  bool bootable,
                                  char partition_type_guid[16],
                                  unsigned char *out)
{
  size_t i, len;
  const char *filename;
  struct gpt_entry *entry = (struct gpt_entry *) out;

  assert (sizeof (struct gpt_entry) == GPT_PT_ENTRY_SIZE);

  memcpy (entry->partition_type_guid, partition_type_guid, 16);

  memcpy (entry->unique_guid, the_files.ptr[region->u.i].guid, 16);

  entry->first_lba = htole64 (region->start / SECTOR_SIZE);
  entry->last_lba = htole64 (region->end / SECTOR_SIZE);
  entry->attributes = htole64 (bootable ? 4 : 0);

  /* If the filename is 7 bit ASCII then this will reproduce it as a
   * UTF-16LE string.
   *
   * Is this a security risk?  It reveals something about paths on the
   * server to clients. XXX
   */
  filename = the_files.ptr[region->u.i].filename;
  len = strlen (filename);
  if (len < 36) {
    for (i = 0; i < len; ++i)
      if ((unsigned char) filename[i] > 127)
        goto out;

    for (i = 0; i < len; ++i) {
      entry->name[2*i] = filename[i];
      entry->name[2*i+1] = 0;
    }
  }
 out: ;
}

static void
create_gpt_protective_mbr (unsigned char *out)
{
  struct region region;
  uint64_t end;

  /* Protective MBR creates a partition with partition ID 0xee which
   * covers the whole of the disk, or as much of the disk as
   * expressible with MBR.
   */
  region.start = 512;
  end = virtual_size (&the_regions) - 1;
  if (end > UINT32_MAX * SECTOR_SIZE)
    end = UINT32_MAX * SECTOR_SIZE;
  region.end = end;
  region.len = region.end - region.start + 1;

  create_mbr_partition_table_entry (&region, false, 0xee, &out[0x1be]);

  /* Boot signature. */
  out[0x1fe] = 0x55;
  out[0x1ff] = 0xaa;
}

/* Try to parse a GPT GUID. */
int
parse_guid (const char *str, char *out)
{
  size_t i;
  size_t len = strlen (str);

  if (len == 36)
    /* nothing */;
  else if (len == 38 && str[0] == '{' && str[37] == '}') {
    str++;
    len -= 2;
  }
  else
    return -1;

  assert (len == 36);

  if (str[8] != '-' || str[13] != '-' || str[18] != '-' || str[23] != '-')
    return -1;

  for (i = 0; i < 8; ++i)
    if (!ascii_isxdigit (str[i]))
      return -1;
  for (i = 9; i < 13; ++i)
    if (!ascii_isxdigit (str[i]))
      return -1;
  for (i = 14; i < 18; ++i)
    if (!ascii_isxdigit (str[i]))
      return -1;
  for (i = 19; i < 23; ++i)
    if (!ascii_isxdigit (str[i]))
      return -1;
  for (i = 24; i < 36; ++i)
    if (!ascii_isxdigit (str[i]))
      return -1;

  /* The first, second and third blocks are parsed as little endian,
   * while the fourth and fifth blocks are big endian.
   */
  *out++ = hexbyte (str[6], str[7]);   /* first block */
  *out++ = hexbyte (str[4], str[5]);
  *out++ = hexbyte (str[2], str[3]);
  *out++ = hexbyte (str[0], str[1]);

  *out++ = hexbyte (str[11], str[12]); /* second block */
  *out++ = hexbyte (str[9], str[10]);

  *out++ = hexbyte (str[16], str[17]); /* third block */
  *out++ = hexbyte (str[14], str[15]);

  *out++ = hexbyte (str[19], str[20]); /* fourth block */
  *out++ = hexbyte (str[21], str[22]);

  *out++ = hexbyte (str[24], str[25]); /* fifth block */
  *out++ = hexbyte (str[26], str[27]);
  *out++ = hexbyte (str[28], str[29]);
  *out++ = hexbyte (str[30], str[31]);
  *out++ = hexbyte (str[32], str[33]);
  *out++ = hexbyte (str[34], str[35]);

  return 0;
}
