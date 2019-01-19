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
#include <inttypes.h>
#include <assert.h>

#include <nbdkit-plugin.h>

#include "isaligned.h"

#include "regions.h"
#include "virtual-disk.h"

static int create_partition_table (void);

/* Called once we have the list of filenames and have selected a
 * partition type.  This creates the virtual disk layout as a list of
 * regions.
 */
int
create_virtual_disk_layout (void)
{
  struct region region;
  size_t i;

  assert (nr_regions (&regions) == 0);
  assert (nr_files > 0);
  assert (primary == NULL);
  assert (secondary == NULL);

  /* Allocate the virtual partition table. */
  if (parttype == PARTTYPE_MBR) {
    primary = calloc (1, SECTOR_SIZE);
    if (primary == NULL) {
      nbdkit_error ("malloc: %m");
      return -1;
    }

    if (nr_files > 4) {
      /* The first 3 primary partitions will be real partitions, the
       * 4th will be an extended partition, and so we need to store
       * EBRs for nr_files-3 logical partitions.
       */
      ebr = malloc (sizeof (unsigned char *) * (nr_files-3));
      if (ebr == NULL) {
        nbdkit_error ("malloc: %m");
        return -1;
      }
      for (i = 0; i < nr_files-3; ++i) {
        ebr[i] = calloc (1, SECTOR_SIZE);
        if (ebr[i] == NULL) {
          nbdkit_error ("malloc: %m");
          return -1;
        }
      }
    }
  }
  else /* PARTTYPE_GPT */ {
    /* Protective MBR + PT header + PTA = 2 + GPT_PTA_LBAs */
    primary = calloc (2+GPT_PTA_LBAs, SECTOR_SIZE);
    if (primary == NULL) {
      nbdkit_error ("malloc: %m");
      return -1;
    }
    /* Secondary PTA + PT secondary header = GPT_PTA_LBAs + 1 */
    secondary = calloc (GPT_PTA_LBAs+1, SECTOR_SIZE);
    if (secondary == NULL) {
      nbdkit_error ("malloc: %m");
      return -1;
    }
  }

  /* Virtual primary partition table region at the start of the disk. */
  if (parttype == PARTTYPE_MBR) {
    region.start = 0;
    region.len = SECTOR_SIZE;
    region.end = region.start + region.len - 1;
    region.type = region_data;
    region.u.data = primary;
    region.description = "MBR";
    if (append_region (&regions, region) == -1)
      return -1;
  }
  else /* PARTTYPE_GPT */ {
    region.start = 0;
    region.len = (2+GPT_PTA_LBAs) * SECTOR_SIZE;
    region.end = region.start + region.len - 1;
    region.type = region_data;
    region.u.data = primary;
    region.description = "GPT primary";
    if (append_region (&regions, region) == -1)
      return -1;
  }

  /* The partitions. */
  for (i = 0; i < nr_files; ++i) {
    uint64_t offset;

    offset = virtual_size (&regions);
    /* Because we add padding after each partition, this invariant
     * must always be true.
     */
    assert (IS_ALIGNED (offset, SECTOR_SIZE));

    /* Logical partitions are preceeded by an EBR. */
    if (parttype == PARTTYPE_MBR && nr_files > 4 && i >= 3) {
      region.start = offset;
      region.len = SECTOR_SIZE;
      region.end = region.start + region.len - 1;
      region.type = region_data;
      region.u.data = ebr[i-3];
      region.description = "EBR";
      if (append_region (&regions, region) == -1)
        return -1;

      offset = virtual_size (&regions);
    }

    /* Make sure each partition is aligned for best performance. */
    if (!IS_ALIGNED (offset, files[i].alignment)) {
      region.start = offset;
      region.end = (offset & ~(files[i].alignment-1)) + files[i].alignment - 1;
      region.len = region.end - region.start + 1;
      region.type = region_zero;
      region.description = "padding before partition";
      if (append_region (&regions, region) == -1)
        return -1;
    }

    offset = virtual_size (&regions);
    assert (IS_ALIGNED (offset, files[i].alignment));

    /* Create the partition region for this file. */
    region.start = offset;
    region.len = files[i].statbuf.st_size;
    region.end = region.start + region.len - 1;
    region.type = region_file;
    region.u.i = i;
    region.description = files[i].filename;
    if (append_region (&regions, region) == -1)
      return -1;

    /* If the file size is not a multiple of SECTOR_SIZE then
     * add a padding region at the end to round it up.
     */
    if (!IS_ALIGNED (files[i].statbuf.st_size, SECTOR_SIZE)) {
      region.start = virtual_size (&regions);
      region.len = SECTOR_SIZE - (files[i].statbuf.st_size & (SECTOR_SIZE-1));
      region.end = region.start + region.len - 1;
      region.type = region_zero;
      region.description = "padding after partition";
      if (append_region (&regions, region) == -1)
        return -1;
    }
  }

  /* For GPT add the virtual secondary/backup partition table. */
  if (parttype == PARTTYPE_GPT) {
    region.start = virtual_size (&regions);
    region.len = (GPT_PTA_LBAs+1) * SECTOR_SIZE;
    region.end = region.start + region.len - 1;
    region.type = region_data;
    region.u.data = secondary;
    region.description = "GPT secondary";
    if (append_region (&regions, region) == -1)
      return -1;
  }

  if (partitioning_debug_regions) {
    for (i = 0; i < nr_regions (&regions); ++i) {
      const struct region *region = get_region (&regions, i);

      nbdkit_debug ("region[%zu]: %" PRIx64 "-%" PRIx64 " type=%s",
                    i, region->start, region->end,
                    region->type == region_file ?
                    files[region->u.i].filename :
                    region->type == region_data ?
                    "data" : "zero");
    }
  }

  /* We must have created some regions. */
  assert (nr_regions (&regions) > 0);

  /* Check the final alignment of all the partitions is the same as
   * what was requested.
   */
  for (i = 0; i < nr_regions (&regions); ++i) {
    const struct region *region = get_region (&regions, i);

    if (region->type == region_file)
      assert (IS_ALIGNED (region->start, files[region->u.i].alignment));
  }

  return create_partition_table ();
}

static int
create_partition_table (void)
{
  /* The caller has already created the disk layout and allocated
   * space in memory for the partition table.
   */
  assert (nr_regions (&regions) > 0);
  assert (primary != NULL);
  if (parttype == PARTTYPE_GPT)
    assert (secondary != NULL);

  if (parttype == PARTTYPE_MBR)
    create_mbr_layout ();
  else /* parttype == PARTTYPE_GPT */
    create_gpt_layout ();

  return 0;
}
