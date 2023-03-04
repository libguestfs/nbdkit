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

#include "random.h"
#include "regions.h"

#include "virtual-disk.h"

static int create_regions (struct virtual_disk *disk);

void
init_virtual_disk (struct virtual_disk *disk)
{
  memset (disk, 0, sizeof *disk);
  disk->fd = -1;

  init_regions (&disk->regions);
}

int
create_virtual_disk (struct virtual_disk *disk)
{
  size_t i;

  /* Allocate the partition table structures.  We can't fill them in
   * until we have created the disk layout.
   */
  disk->protective_mbr = calloc (1, SECTOR_SIZE);
  disk->primary_header = calloc (1, SECTOR_SIZE);
  disk->pt = calloc (1, 32*SECTOR_SIZE);
  disk->secondary_header = calloc (1, SECTOR_SIZE);
  if (disk->protective_mbr == NULL ||
      disk->primary_header == NULL ||
      disk->pt == NULL ||
      disk->secondary_header == NULL) {
    nbdkit_error ("calloc: %m");
    return -1;
  }

  /* Create the filesystem.  This fills in disk->filesystem_size and
   * disk->id.
   */
  if (create_filesystem (disk) == -1)
    return -1;

  /* Create a random GUID used as "Unique partition GUID".  However
   * this doesn't follow GUID conventions so in theory could make an
   * invalid value.
   */
  for (i = 0; i < 16; ++i)
    disk->guid[i] = xrandom (&random_state) & 0xff;

  /* Create the virtual disk regions. */
  if (create_regions (disk) == -1)
    return -1;

  /* Initialize partition table structures.  This depends on
   * disk->regions so must be done last.
   */
  if (create_partition_table (disk) == -1)
    return -1;

  return 0;
}

void
free_virtual_disk (struct virtual_disk *disk)
{
  free_regions (&disk->regions);
  free (disk->protective_mbr);
  free (disk->primary_header);
  free (disk->pt);
  free (disk->secondary_header);
  if (disk->fd >= 0)
    close (disk->fd);
}

/* Lay out the final disk. */
static int
create_regions (struct virtual_disk *disk)
{
  /* Protective MBR. */
  if (append_region_len (&disk->regions, "Protective MBR",
                         SECTOR_SIZE, 0, 0,
                         region_data, (void *) disk->protective_mbr) == -1)
    return -1;

  /* GPT primary partition table header (LBA 1). */
  if (append_region_len (&disk->regions, "GPT primary header",
                         SECTOR_SIZE, 0, 0,
                         region_data, (void *) disk->primary_header) == -1)
    return -1;

  /* GPT primary PT (LBA 2..33). */
  if (append_region_len (&disk->regions, "GPT primary PT",
                         32*SECTOR_SIZE, 0, 0,
                         region_data, (void *) disk->pt) == -1)
    return -1;

  /* Partition containing the filesystem.  Align it to 2048 sectors. */
  if (append_region_len (&disk->regions, "Filesystem",
                         disk->filesystem_size, 2048*SECTOR_SIZE, 0,
                         region_file, 0 /* unused */) == -1)
    return -1;

  /* GPT secondary PT (LBA -33..-2). */
  if (append_region_len (&disk->regions, "GPT secondary PT",
                         32*SECTOR_SIZE, SECTOR_SIZE, 0,
                         region_data, (void *) disk->pt) == -1)
    return -1;

  /* GPT secondary PT header (LBA -1). */
  if (append_region_len (&disk->regions, "GPT secondary header",
                         SECTOR_SIZE, 0, 0,
                         region_data, (void *) disk->secondary_header) == -1)
    return -1;

  return 0;
}
