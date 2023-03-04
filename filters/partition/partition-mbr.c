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
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#include <nbdkit-filter.h>

#include "byte-swapping.h"
#include "isaligned.h"

#include "partition.h"

/* See also linux.git/block/partitions/msdos.c:is_extended_partition */
#define is_extended(byte) ((byte) == 0x5 || (byte) == 0xf || (byte) == 0x85)

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
find_mbr_partition (nbdkit_next *next,
                    int64_t size, uint8_t *mbr,
                    int64_t *offset_r, int64_t *range_r)
{
  int i;
  struct mbr_partition partition;
  uint32_t ep_start_sector, ep_nr_sectors;
  uint64_t ebr, next_ebr;
  uint8_t sector[SECTOR_SIZE];

  /* Primary partition. */
  if (partnum <= 4) {
    for (i = 0; i < 4; ++i) {
      get_mbr_partition (mbr, i, &partition);
      if (partition.nr_sectors > 0 &&
          partition.part_type_byte != 0 &&
          !is_extended (partition.part_type_byte) &&
          partnum == i+1) {
        *offset_r = partition.start_sector * (int64_t) SECTOR_SIZE;
        *range_r = partition.nr_sectors * (int64_t) SECTOR_SIZE;
        return 0;
      }
    }

    /* Not found falls through to error case at the end of the function. */
  }

  /* Logical partition. */
  else {
    /* Find the extended partition in the primary partition table. */
    for (i = 0; i < 4; ++i) {
      get_mbr_partition (mbr, i, &partition);
      if (partition.nr_sectors > 0 &&
          is_extended (partition.part_type_byte)) {
        goto found_extended;
      }
    }
    nbdkit_error ("MBR logical partition selected, "
                  "but there is no extended partition in the partition table");
    return -1;

  found_extended:
    ep_start_sector = partition.start_sector;
    ep_nr_sectors = partition.nr_sectors;
    ebr = ep_start_sector * (uint64_t)SECTOR_SIZE;

    /* This loop will terminate eventually because we only accept
     * links which strictly increase the EBR pointer.  There are valid
     * partition tables which do odd things like arranging the
     * partitions in reverse order, but we will not accept them here.
     */
    for (i = 5; ; ++i) {
      /* Check that the ebr is aligned and pointing inside the disk
       * and doesn't point to the MBR.
       */
      if (!IS_ALIGNED (ebr, SECTOR_SIZE) ||
          ebr < SECTOR_SIZE || ebr >= size-SECTOR_SIZE) {
        nbdkit_error ("invalid EBR chain: "
                      "next EBR boot sector is located outside disk boundary");
        return -1;
      }

      /* Read the EBR sector. */
      nbdkit_debug ("partition: reading EBR at %" PRIi64, ebr);
      if (next->pread (next, sector, sizeof sector, ebr, 0, &errno) == -1)
        return -1;

      if (partnum == i) {
        uint64_t offset, range;

        /* First entry in EBR points to the logical partition. */
        get_mbr_partition (sector, 0, &partition);

        /* The first entry start sector is relative to the EBR. */
        offset = ebr + partition.start_sector * (uint64_t)SECTOR_SIZE;
        range = partition.nr_sectors * (uint64_t)SECTOR_SIZE;

        /* Logical partition cannot be before the corresponding EBR,
         * and it cannot extend beyond the enclosing extended
         * partition.
         */
        if (offset <= ebr ||
            offset + range >
            ((uint64_t)ep_start_sector + ep_nr_sectors) * SECTOR_SIZE) {
          nbdkit_error ("logical partition start or size out of range "
                        "(offset=%" PRIu64 ", range=%" PRIu64 ", "
                        "ep:startsect=%" PRIu32 ", ep:nrsects=%" PRIu32 ")",
                        offset, range, ep_start_sector, ep_nr_sectors);
          return -1;
        }
        *offset_r = offset;
        *range_r = range;
        return 0;
      }

      /* Second entry in EBR links to the next EBR. */
      get_mbr_partition (sector, 1, &partition);

      /* All zeroes means the end of the chain. */
      if (partition.start_sector == 0 && partition.nr_sectors == 0)
        break;

      /* The second entry start sector is relative to the start to the
       * extended partition.
       */
      next_ebr =
        ((uint64_t)ep_start_sector + partition.start_sector) * SECTOR_SIZE;

      /* Make sure the next EBR > current EBR. */
      if (next_ebr <= ebr) {
        nbdkit_error ("invalid EBR chain: "
                      "next EBR %" PRIu64 " <= current EBR %" PRIu64,
                      next_ebr, ebr);
        return -1;
      }
      ebr = next_ebr;
    }
  }

  nbdkit_error ("MBR partition %d not found", partnum);
  return -1;
}
