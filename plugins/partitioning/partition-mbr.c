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

#include "byte-swapping.h"

#include "isaligned.h"
#include "rounding.h"

#include "regions.h"
#include "virtual-disk.h"

static const struct region *find_file_region (size_t i, size_t *j);
static const struct region *find_ebr_region (size_t i, size_t *j);

/* Create the MBR and optionally EBRs. */
void
create_mbr_layout (void)
{
  size_t i, j = 0;

  /* Boot signature. */
  primary[0x1fe] = 0x55;
  primary[0x1ff] = 0xaa;

  if (the_files.len <= 4) {
    /* Basic MBR with no extended partition. */
    for (i = 0; i < the_files.len; ++i) {
      const struct region *region = find_file_region (i, &j);

      create_mbr_partition_table_entry (region, i == 0, the_files.ptr[i].mbr_id,
                                        &primary[0x1be + 16*i]);
    }
  }
  else {
    struct region region;
    const struct region *rptr, *eptr0, *eptr;

    /* The first three primary partitions correspond to the first
     * three files.
     */
    for (i = 0; i < 3; ++i) {
      rptr = find_file_region (i, &j);
      create_mbr_partition_table_entry (rptr, i == 0, the_files.ptr[i].mbr_id,
                                        &primary[0x1be + 16*i]);
    }

    /* The fourth partition is an extended PTE and does not correspond
     * to any file.  This partition starts with the first EBR, so find
     * it.  The partition extends to the end of the disk.
     */
    eptr0 = find_ebr_region (3, &j);
    region.start = eptr0->start;
    region.end = virtual_size (&the_regions) - 1; /* to end of disk */
    region.len = region.end - region.start + 1;
    create_mbr_partition_table_entry (&region, false, 0xf, &primary[0x1ee]);

    /* The remaining files are mapped to logical partitions living in
     * the fourth extended partition.
     */
    for (i = 3; i < the_files.len; ++i) {
      if (i == 3)
        eptr = eptr0;
      else
        eptr = find_ebr_region (i, &j);
      rptr = find_file_region (i, &j);

      /* Signature. */
      ebr[i-3][0x1fe] = 0x55;
      ebr[i-3][0x1ff] = 0xaa;

      /* First entry in EBR contains:
       * offset from EBR sector to the first sector of the logical partition
       * total count of sectors in the logical partition
       */
      region.start = rptr->start - eptr->start;
      region.len = rptr->len;
      create_mbr_partition_table_entry (&region, false, the_files.ptr[i].mbr_id,
                                        &ebr[i-3][0x1be]);

      if (i < the_files.len-1) {
        size_t j2 = j;
        const struct region *enext = find_ebr_region (i+1, &j2);
        const struct region *rnext = find_file_region (i+1, &j2);

        /* Second entry in the EBR contains:
         * address of next EBR relative to extended partition
         * total count of sectors in the next logical partition including
         * next EBR
         */
        region.start = enext->start - eptr0->start;
        region.len = rnext->end - enext->start + 1;
        create_mbr_partition_table_entry (&region, false, 0xf,
                                          &ebr[i-3][0x1ce]);
      }
    }
  }
}

/* Find the region corresponding to file[i].
 * j is a scratch register ensuring we only do a linear scan.
 */
static const struct region *
find_file_region (size_t i, size_t *j)
{
  const struct region *region;

  for (; *j < nr_regions (&the_regions); ++(*j)) {
    region = &the_regions.ptr[*j];
    if (region->type == region_file && region->u.i == i)
      return region;
  }
  abort ();
}

/* Find the region corresponding to EBR of file[i] (i >= 3).
 * j is a scratch register ensuring we only do a linear scan.
 */
static const struct region *
find_ebr_region (size_t i, size_t *j)
{
  const struct region *region;

  assert (i >= 3);

  for (; *j < nr_regions (&the_regions); ++(*j)) {
    region = &the_regions.ptr[*j];
    if (region->type == region_data && region->u.data == ebr[i-3])
      return region;
  }
  abort ();
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
                                  bool bootable, int partition_id,
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
