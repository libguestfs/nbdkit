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
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <endian.h>

#include <nbdkit-filter.h>

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

static int partnum = -1;

/* Called for each key=value passed on the command line. */
static int
partition_config (nbdkit_next_config *next, void *nxdata,
                  const char *key, const char *value)
{
  if (strcmp (key, "partition") == 0) {
    if (sscanf (value, "%d", &partnum) != 1 || partnum <= 0) {
      nbdkit_error ("invalid partition number");
      return -1;
    }
    return 0;
  }
  else
    return next (nxdata, key, value);
}

/* Check the user did pass partition number. */
static int
partition_config_complete (nbdkit_next_config_complete *next, void *nxdata)
{
  if (partnum == -1) {
    nbdkit_error ("you must supply the partition parameter on the command line");
    return -1;
  }

  return next (nxdata);
}

#define partition_config_help \
  "partition=<PART>    (required) The partition number (counting from 1)."

struct handle {
  int64_t offset;
  int64_t range;
};

/* Open a connection. */
static void *
partition_open (nbdkit_next_open *next, void *nxdata, int readonly)
{
  struct handle *h;

  if (next (nxdata, readonly) == -1)
    return NULL;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  /* These are set in the prepare method. */
  h->offset = h->range = -1;
  return h;
}

static void
partition_close (void *handle)
{
  struct handle *h = handle;

  free (h);
}

/* Inspect the underlying partition table.  partition_prepare is
 * called before data processing.
 */
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

static int
find_mbr_partition (struct nbdkit_next_ops *next_ops, void *nxdata,
                    struct handle *h, int64_t size, uint8_t *mbr)
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
      h->offset = partition.start_sector * 512;
      h->range = partition.nr_sectors * 512;
      return 0;
    }
  }

  nbdkit_error ("MBR partition %d not found", partnum);
  return -1;
}

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

static int
find_gpt_partition (struct nbdkit_next_ops *next_ops, void *nxdata,
                    struct handle *h, int64_t size, uint8_t *header_bytes)
{
  uint8_t partition_bytes[128];
  struct gpt_header header;
  struct gpt_partition partition;
  int i;
  int err;

  if (partnum > 128) {
  out_of_range:
    nbdkit_error ("GPT partition number out of range");
    return -1;
  }
  get_gpt_header (header_bytes, &header);
  if (partnum > header.nr_partitions)
    goto out_of_range;

  if (header.partition_entry_size != 128) {
    nbdkit_error ("GPT partition entry is not 128 bytes");
    return -1;
  }

  for (i = 0; i < 128; ++i) {
    /* We already checked these are within bounds in the
     * partition_prepare call above.
     */
    if (next_ops->pread (nxdata, partition_bytes, sizeof partition_bytes,
                         2*512 + i*128, 0, &err) == -1)
      return -1;
    get_gpt_partition (partition_bytes, &partition);
    if (memcmp (partition.partition_type_guid,
                "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) != 0 &&
        partnum == i+1) {
      h->offset = partition.first_lba * 512;
      h->range = (1 + partition.last_lba - partition.first_lba) * 512;
      return 0;
    }
  }

  nbdkit_error ("GPT partition %d not found", partnum);
  return -1;
}

static int
partition_prepare (struct nbdkit_next_ops *next_ops, void *nxdata,
                   void *handle)
{
  struct handle *h = handle;
  int64_t size;
  uint8_t lba01[1024];          /* LBA 0 and 1 */
  int r;
  int err;

  size = next_ops->get_size (nxdata);
  if (size == -1)
    return -1;
  if (size < 1024) {
    nbdkit_error ("disk is too small to be a partitioned disk");
    return -1;
  }

  nbdkit_debug ("disk size=%" PRIi64, size);

  if (next_ops->pread (nxdata, lba01, sizeof lba01, 0, 0, &err) == -1)
    return -1;

  /* Is it GPT? */
  if (size >= 2 * 34 * 512 && memcmp (&lba01[512], "EFI PART", 8) == 0)
    r = find_gpt_partition (next_ops, nxdata, h, size, &lba01[512]);
  /* Is it MBR? */
  else if (lba01[0x1fe] == 0x55 && lba01[0x1ff] == 0xAA)
    r = find_mbr_partition (next_ops, nxdata, h, size, lba01);
  else {
    nbdkit_error ("disk does not contain MBR or GPT partition table signature");
    r = -1;
  }
  if (r == -1)
    return -1;

  /* The find_*_partition functions set h->offset & h->range in the
   * handle to point to the partition boundaries.  However we
   * additionally check that they are inside the underlying disk.
   */
  if (h->offset < 0 || h->range < 0 || h->offset + h->range > size) {
    nbdkit_error ("partition is outside the disk");
    return -1;
  }

  nbdkit_debug ("partition offset=%" PRIi64 " range=%" PRIi64,
                h->offset, h->range);

  return 0;
}

/* Get the file size. */
static int64_t
partition_get_size (struct nbdkit_next_ops *next_ops, void *nxdata,
                    void *handle)
{
  struct handle *h = handle;

  return h->range;
}

/* Read data. */
static int
partition_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
                 void *handle, void *buf, uint32_t count, uint64_t offs,
                 uint32_t flags, int *err)
{
  struct handle *h = handle;

  return next_ops->pread (nxdata, buf, count, offs + h->offset, flags, err);
}

/* Write data. */
static int
partition_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
                  void *handle,
                  const void *buf, uint32_t count, uint64_t offs,
                  uint32_t flags, int *err)
{
  struct handle *h = handle;

  return next_ops->pwrite (nxdata, buf, count, offs + h->offset, flags, err);
}

/* Trim data. */
static int
partition_trim (struct nbdkit_next_ops *next_ops, void *nxdata,
                void *handle, uint32_t count, uint64_t offs, uint32_t flags,
                int *err)
{
  struct handle *h = handle;

  return next_ops->trim (nxdata, count, offs + h->offset, flags, err);
}

/* Zero data. */
static int
partition_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
                void *handle, uint32_t count, uint64_t offs, uint32_t flags,
                int *err)
{
  struct handle *h = handle;

  return next_ops->zero (nxdata, count, offs + h->offset, flags, err);
}

static struct nbdkit_filter filter = {
  .name              = "partition",
  .longname          = "nbdkit partition filter",
  .version           = PACKAGE_VERSION,
  .config            = partition_config,
  .config_complete   = partition_config_complete,
  .config_help       = partition_config_help,
  .open              = partition_open,
  .prepare           = partition_prepare,
  .close             = partition_close,
  .get_size          = partition_get_size,
  .pread             = partition_pread,
  .pwrite            = partition_pwrite,
  .trim              = partition_trim,
  .zero              = partition_zero,
};

NBDKIT_REGISTER_FILTER(filter)
