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
#include <inttypes.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <nbdkit-plugin.h>

#include "byte-swapping.h"
#include "isaligned.h"
#include "iszero.h"
#include "rounding.h"

#include "crc32.h"

/* Debug flag: -D partitioning.regions=1: Print the regions table. */
int partitioning_debug_regions;

#define SECTOR_SIZE UINT64_C(512)

/* Maximum size of MBR disks.  This is an approximation based on the
 * known limit (2^32 sectors) and an estimate based on the amount of
 * padding between partitions.
 */
#define MAX_MBR_DISK_SIZE (UINT32_MAX * SECTOR_SIZE - 5 * MAX_ALIGNMENT)

#define GPT_MAX_PARTITIONS 128
#define GPT_PT_ENTRY_SIZE 128

/* Maximum possible and default alignment between partitions. */
#define MAX_ALIGNMENT (2048 * SECTOR_SIZE)
#define DEFAULT_ALIGNMENT MAX_ALIGNMENT

/* Default MBR partition ID and GPT partition type GUID. */
#define DEFAULT_MBR_ID 0x83
#define DEFAULT_TYPE_GUID "0FC63DAF-8483-4772-8E79-3D69D8477DE4"

/* alignment, mbr_id, type_guid set on the command line for
 * following partitions.
 */
static unsigned long alignment = DEFAULT_ALIGNMENT;
static int mbr_id = DEFAULT_MBR_ID;
static char type_guid[16] /* set by partitioning_load function below */;

/* Files supplied on the command line. */
struct file {
  const char *filename;         /* file= supplied on the command line */
  int fd;
  struct stat statbuf;
  char guid[16];                /* random GUID used for GPT */
  unsigned long alignment;      /* alignment of this partition */
  int mbr_id;                   /* MBR ID of this partition */
  char type_guid[16];           /* partition type GUID of this partition */
};

static struct file *files = NULL;
static size_t nr_files = 0;

/* partition-type parameter. */
#define PARTTYPE_UNSET 0
#define PARTTYPE_MBR   1
#define PARTTYPE_GPT   2
static int parttype = PARTTYPE_UNSET;

/* Virtual disk regions (contiguous). */
enum region_type {
  region_file,        /* contents of the i'th file */
  region_data,        /* pointer to data (used for partition table) */
  region_zero,        /* padding */
};

struct region {
  uint64_t start, len, end;    /* byte offsets; end = start + len - 1 */
  enum region_type type;
  union {
    size_t i;                  /* region_file: i'th file */
    const unsigned char *data; /* region_data: data (partition table) */
  } u;
};

static struct region *regions = NULL;
static size_t nr_regions = 0;

/* Primary and secondary partition tables (secondary is only used for GPT). */
static unsigned char *primary = NULL, *secondary = NULL;

static int parse_guid (const char *str, char *out);

static void
partitioning_load (void)
{
  srandom (time (NULL));
  parse_guid (DEFAULT_TYPE_GUID, type_guid);
}

static void
partitioning_unload (void)
{
  size_t i;

  for (i = 0; i < nr_files; ++i)
    close (files[i].fd);
  free (files);

  /* We don't need to free regions[].u.data because it points to
   * either primary or secondary which we free here.
   */
  free (regions);
  free (primary);
  free (secondary);
}

/* Find the region corresponding to the given offset.  Use region->end
 * to find the end of the region.
 */
static int
compare_offset (const void *offsetp, const void *regionp)
{
  const uint64_t offset = *(uint64_t *)offsetp;
  const struct region *region = (struct region *)regionp;

  if (offset < region->start) return -1;
  if (offset > region->end) return 1;
  return 0;
}

static struct region *
get_region (uint64_t offset)
{
  return bsearch (&offset, regions, nr_regions, sizeof (struct region),
                  compare_offset);
}

/* Helper function to expand an array of objects. */
static int
expand (void **objects, size_t size, size_t *nr_objects)
{
  void *p;

  p = realloc (*objects, (*nr_objects+1) * size);
  if (p == NULL) {
    nbdkit_error ("realloc: %m");
    return -1;
  }
  *objects = p;
  (*nr_objects)++;
  return 0;
}

/* Called once we have the list of filenames and have selected a
 * partition type.  This creates the virtual disk layout as a list of
 * regions.
 */
static int create_partition_table (void);

static int
create_virtual_disk_layout (void)
{
  struct region region;
  size_t i;

  assert (nr_regions == 0);
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
  }
  else /* PARTTYPE_GPT */ {
    primary = calloc (34, SECTOR_SIZE);
    if (primary == NULL) {
      nbdkit_error ("malloc: %m");
      return -1;
    }
    secondary = calloc (33, SECTOR_SIZE);
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
    if (expand ((void *) &regions, sizeof (struct region), &nr_regions) == -1)
      return -1;
    regions[nr_regions-1] = region;
  }
  else /* PARTTYPE_GPT */ {
    region.start = 0;
    region.len = 34 * SECTOR_SIZE;
    region.end = region.start + region.len - 1;
    region.type = region_data;
    region.u.data = primary;
    if (expand ((void *)&regions, sizeof (struct region), &nr_regions) == -1)
      return -1;
    regions[nr_regions-1] = region;
  }

  /* The partitions. */
  for (i = 0; i < nr_files; ++i) {
    uint64_t offset;

    offset = regions[nr_regions-1].end + 1;
    /* Because we add padding after each partition, this invariant
     * must always be true.
     */
    assert (IS_ALIGNED (offset, SECTOR_SIZE));

    /* Make sure each partition is aligned for best performance. */
    if (!IS_ALIGNED (offset, files[i].alignment)) {
      region.start = offset;
      region.end = (offset & ~(files[i].alignment-1)) + files[i].alignment - 1;
      region.len = region.end - region.start + 1;
      region.type = region_zero;
      if (expand ((void *)&regions, sizeof (struct region), &nr_regions) == -1)
        return -1;
      regions[nr_regions-1] = region;
    }

    offset = regions[nr_regions-1].end + 1;
    assert (IS_ALIGNED (offset, files[i].alignment));

    /* Create the partition region for this file. */
    region.start = offset;
    region.len = files[i].statbuf.st_size;
    region.end = region.start + region.len - 1;
    region.type = region_file;
    region.u.i = i;
    if (expand ((void *)&regions, sizeof (struct region), &nr_regions) == -1)
      return -1;
    regions[nr_regions-1] = region;

    /* If the file size is not a multiple of SECTOR_SIZE then
     * add a padding region at the end to round it up.
     */
    if (!IS_ALIGNED (files[i].statbuf.st_size, SECTOR_SIZE)) {
      region.start = regions[nr_regions-1].end + 1;
      region.len = SECTOR_SIZE - (files[i].statbuf.st_size & (SECTOR_SIZE-1));
      region.end = region.start + region.len - 1;
      region.type = region_zero;
      if (expand ((void *)&regions, sizeof (struct region), &nr_regions) == -1)
        return -1;
      regions[nr_regions-1] = region;
    }
  }

  /* For GPT add the virtual secondary/backup partition table. */
  if (parttype == PARTTYPE_GPT) {
    region.start = regions[nr_regions-1].end + 1;
    region.len = 33 * SECTOR_SIZE;
    region.end = region.start + region.len - 1;
    region.type = region_data;
    region.u.data = secondary;
    if (expand ((void *)&regions, sizeof (struct region), &nr_regions) == -1)
      return -1;
    regions[nr_regions-1] = region;
  }

  if (partitioning_debug_regions) {
    for (i = 0; i < nr_regions; ++i) {
      nbdkit_debug ("region[%zu]: %" PRIx64 "-%" PRIx64 " type=%s",
                    i, regions[i].start, regions[i].end,
                    regions[i].type == region_file ?
                    files[regions[i].u.i].filename :
                    regions[i].type == region_data ?
                    "data" : "zero");
    }
  }

  /* Assert that the regions table looks sane. */
  assert (nr_regions > 0);
  assert (regions[0].start == 0);
  for (i = 0; i < nr_regions; ++i) {
    assert (regions[i].len > 0);
    assert (regions[i].end >= regions[i].start);
    assert (regions[i].len == regions[i].end - regions[i].start + 1);
    if (i+1 < nr_regions) {
      assert (regions[i].end + 1 == regions[i+1].start);
    }
  }

  return create_partition_table ();
}

/* Create the partition table (and for GPT the secondary/backup). */
static void create_mbr_partition_table (unsigned char *out);
static void create_mbr_partition_table_entry (const struct region *, int bootable, int partition_id, unsigned char *);
static void create_gpt_partition_header (const void *pt, int is_primary, unsigned char *out);
static void create_gpt_partition_table (unsigned char *out);
static void create_gpt_partition_table_entry (const struct region *region, int bootable, char partition_type_guid[16], unsigned char *out);
static void create_gpt_protective_mbr (unsigned char *out);

static int
create_partition_table (void)
{
  /* The caller has already create the disk layout and allocated space
   * in memory for the partition table.
   */
  assert (nr_regions > 0);
  assert (primary != NULL);
  if (parttype == PARTTYPE_GPT)
    assert (secondary != NULL);

  if (parttype == PARTTYPE_MBR) {
    assert (nr_files <= 4);
    create_mbr_partition_table (primary);
  }
  else /* parttype == PARTTYPE_GPT */ {
    void *pt;

    assert (nr_files <= GPT_MAX_PARTITIONS);

    /* Protective MBR.  LBA 0 */
    create_gpt_protective_mbr (primary);

    /* Primary partition table.  LBA 2-33 */
    pt = &primary[2*SECTOR_SIZE];
    create_gpt_partition_table (pt);

    /* Partition table header.  LBA 1 */
    create_gpt_partition_header (pt, 1, &primary[SECTOR_SIZE]);

    /* Backup partition table.  LBA -33 */
    pt = secondary;
    create_gpt_partition_table (pt);

    /* Backup partition table header.  LBA -1 */
    create_gpt_partition_header (pt, 0, &secondary[32*SECTOR_SIZE]);
  }

  return 0;
}

static void
create_mbr_partition_table (unsigned char *out)
{
  size_t i, j;

  for (j = 0; j < nr_regions; ++j) {
    if (regions[j].type == region_file) {
      i = regions[j].u.i;
      assert (i < 4);
      create_mbr_partition_table_entry (&regions[j], i == 0,
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

static void
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

static void
create_gpt_partition_header (const void *pt, int is_primary,
                             unsigned char *out)
{
  uint64_t nr_lbas;
  struct gpt_header {
    char signature[8];
    char revision[4];
    uint32_t header_size;
    uint32_t crc;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    char guid[16];
    uint64_t partition_entries_lba;
    uint32_t nr_partition_entries;
    uint32_t size_partition_entry;
    uint32_t crc_partitions;
  } *header = (struct gpt_header *) out;

  nr_lbas = (regions[nr_regions-1].end + 1) / SECTOR_SIZE;

  memset (header, 0, sizeof *header);
  memcpy (header->signature, "EFI PART", 8);
  memcpy (header->revision, "\0\0\1\0", 4); /* revision 1.0 */
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
  header->nr_partition_entries = htole32 (GPT_MAX_PARTITIONS);
  header->size_partition_entry = htole32 (GPT_PT_ENTRY_SIZE);
  header->crc_partitions =
    htole32 (crc32 (pt, GPT_PT_ENTRY_SIZE * GPT_MAX_PARTITIONS));

  /* Must be computed last. */
  header->crc = htole32 (crc32 (header, sizeof *header));
}

static void
create_gpt_partition_table (unsigned char *out)
{
  size_t i, j;

  for (j = 0; j < nr_regions; ++j) {
    if (regions[j].type == region_file) {
      i = regions[j].u.i;
      assert (i < GPT_MAX_PARTITIONS);
      create_gpt_partition_table_entry (&regions[j], i == 0,
                                        files[i].type_guid,
                                        out);
      out += GPT_PT_ENTRY_SIZE;
    }
  }
}

static void
create_gpt_partition_table_entry (const struct region *region,
                                  int bootable, char partition_type_guid[16],
                                  unsigned char *out)
{
  size_t i, len;
  const char *filename;
  struct gpt_entry {
    char partition_type_guid[16];
    char unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    char name[72];              /* UTF-16LE */
  } *entry = (struct gpt_entry *) out;

  assert (sizeof (struct gpt_entry) == GPT_PT_ENTRY_SIZE);

  memcpy (entry->partition_type_guid, partition_type_guid, 16);

  memcpy (entry->unique_guid, files[region->u.i].guid, 16);

  entry->first_lba = htole64 (region->start / SECTOR_SIZE);
  entry->last_lba = htole64 (region->end / SECTOR_SIZE);
  entry->attributes = htole64 (bootable ? 4 : 0);

  /* If the filename is 7 bit ASCII then this will reproduce it as a
   * UTF-16LE string.
   *
   * Is this a security risk?  It reveals something about paths on the
   * server to clients. XXX
   */
  filename = files[region->u.i].filename;
  len = strlen (filename);
  if (len < 36) {
    for (i = 0; i < len; ++i)
      if (filename[i] > 127)
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
  end = regions[nr_regions-1].end;
  if (end > UINT32_MAX * SECTOR_SIZE)
    end = UINT32_MAX * SECTOR_SIZE;
  region.end = end;
  region.len = region.end - region.start + 1;

  create_mbr_partition_table_entry (&region, 0, 0xee, &out[0x1be]);

  /* Boot signature. */
  out[0x1fe] = 0x55;
  out[0x1ff] = 0xaa;
}

/* Try to parse a GPT GUID. */
static unsigned char
hexdigit (const char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  else if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  else /* if (c >= 'A' && c <= 'F') */
    return c - 'A' + 10;
}

static unsigned char
hexbyte (const char *p)
{
  unsigned char c0, c1;

  c0 = hexdigit (p[0]);
  c1 = hexdigit (p[1]);
  return c0 << 4 | c1;
}

static int
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
    if (!isxdigit (str[i]))
      return -1;
  for (i = 9; i < 13; ++i)
    if (!isxdigit (str[i]))
      return -1;
  for (i = 14; i < 18; ++i)
    if (!isxdigit (str[i]))
      return -1;
  for (i = 19; i < 23; ++i)
    if (!isxdigit (str[i]))
      return -1;
  for (i = 24; i < 36; ++i)
    if (!isxdigit (str[i]))
      return -1;

  /* The first, second and third blocks are parsed as little endian,
   * while the fourth and fifth blocks are big endian.
   */
  *out++ = hexbyte (&str[6]);   /* first block */
  *out++ = hexbyte (&str[4]);
  *out++ = hexbyte (&str[2]);
  *out++ = hexbyte (&str[0]);

  *out++ = hexbyte (&str[11]);  /* second block */
  *out++ = hexbyte (&str[9]);

  *out++ = hexbyte (&str[16]);  /* third block */
  *out++ = hexbyte (&str[14]);

  *out++ = hexbyte (&str[19]);  /* fourth block */
  *out++ = hexbyte (&str[21]);

  *out++ = hexbyte (&str[24]);  /* fifth block */
  *out++ = hexbyte (&str[26]);
  *out++ = hexbyte (&str[28]);
  *out++ = hexbyte (&str[30]);
  *out++ = hexbyte (&str[32]);
  *out++ = hexbyte (&str[34]);

  return 0;
}

static int
partitioning_config (const char *key, const char *value)
{
  struct file file;
  size_t i;
  int err;

  if (strcmp (key, "file") == 0) {
    file.filename = value;
    file.alignment = alignment;
    file.mbr_id = mbr_id;
    memcpy (file.type_guid, type_guid, sizeof type_guid);

    file.fd = open (file.filename, O_RDWR);
    if (file.fd == -1) {
      nbdkit_error ("%s: %m", file.filename);
      return -1;
    }
    if (fstat (file.fd, &file.statbuf) == -1) {
      err = errno;
      close (file.fd);
      errno = err;
      nbdkit_error ("%s: stat: %m", file.filename);
      return -1;
    }

    if (file.statbuf.st_size == 0) {
      nbdkit_error ("%s: zero length partitions are not allowed",
                    file.filename);
      return -1;
    }

    /* Create a random GUID used as "Unique partition GUID".  However
     * this doesn't follow GUID conventions so in theory could make an
     * invalid value.  This is only used by GPT, and we store it in
     * the file structure because it must be the same across primary
     * and secondary PT entries.
     */
    for (i = 0; i < 16; ++i)
      file.guid[i] = random () & 0xff;

    if (expand ((void *)&files, sizeof (struct file), &nr_files) == -1) {
      err = errno;
      close (file.fd);
      errno = err;
      return -1;
    }
    files[nr_files-1] = file;
  }
  else if (strcmp (key, "partition-type") == 0) {
    if (strcasecmp (value, "mbr") == 0 || strcasecmp (value, "dos") == 0)
      parttype = PARTTYPE_MBR;
    else if (strcasecmp (value, "gpt") == 0)
      parttype = PARTTYPE_GPT;
    else {
      nbdkit_error ("unknown partition-type: %s", value);
      return -1;
    }
  }
  else if (strcmp (key, "alignment") == 0) {
    if (sscanf (value, "%lu", &alignment) != 1) {
      nbdkit_error ("could not parse partition alignment: %s", value);
      return -1;
    }
    if (!(alignment >= SECTOR_SIZE && alignment <= MAX_ALIGNMENT)) {
      nbdkit_error ("partition alignment %lu should be "
                    ">= sector size %lu and <= maximum alignment %lu",
                    alignment,
                    (unsigned long) SECTOR_SIZE,
                    (unsigned long) MAX_ALIGNMENT);
      return -1;
    }
  }
  else if (strcmp (key, "mbr-id") == 0) {
    if (sscanf (value, "%i", &mbr_id) != 1) {
      nbdkit_error ("could not parse mbr-id: %s", value);
      return -1;
    }
  }
  else if (strcmp (key, "type-guid") == 0) {
    if (parse_guid (value, type_guid) == -1) {
      nbdkit_error ("could not validate GUID: %s", value);
      return -1;
    }
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int
partitioning_config_complete (void)
{
  size_t i;
  uint64_t total_size;
  int needs_gpt;

  /* Not enough / too many files? */
  if (nr_files == 0) {
    nbdkit_error ("at least one file= parameter must be supplied");
    return -1;
  }
  if (nr_files > GPT_MAX_PARTITIONS) {
    nbdkit_error ("too many files, the plugin supports a maximum of %d files",
                  GPT_MAX_PARTITIONS);
    return -1;
  }

  total_size = 0;
  for (i = 0; i < nr_files; ++i)
    total_size += files[i].statbuf.st_size;

  if (nr_files > 4)
    needs_gpt = 1;
  else if (total_size > MAX_MBR_DISK_SIZE)
    needs_gpt = 1;
  else
    needs_gpt = 0;

  /* Choose default parttype if not set. */
  if (parttype == PARTTYPE_UNSET) {
    if (needs_gpt) {
      parttype = PARTTYPE_GPT;
      nbdkit_debug ("picking partition type GPT");
    }
    else {
      parttype = PARTTYPE_MBR;
      nbdkit_debug ("picking partition type MBR");
    }
  }
  else if (parttype == PARTTYPE_MBR && needs_gpt) {
    nbdkit_error ("MBR partition table type supports a maximum of 4 partitions and a maximum virtual disk size of about 2 TB, but you requested %zu partition(s) and a total size of %" PRIu64 " bytes (> %" PRIu64 ").  Try using: partition-type=gpt",
                  nr_files, total_size, (uint64_t) MAX_MBR_DISK_SIZE);
    return -1;
  }

  return create_virtual_disk_layout ();
}

#define partitioning_config_help \
  "file=<FILENAME>  (required) File(s) containing partitions\n" \
  "partition-type=mbr|gpt      Partition type"

/* Create the per-connection handle. */
static void *
partitioning_open (int readonly)
{
  /* We don't need a handle.  This is a non-NULL pointer we can return. */
  static int h;

  return &h;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Get the disk size. */
static int64_t
partitioning_get_size (void *handle)
{
  assert (nr_regions > 0);
  return regions[nr_regions-1].end + 1;
}

/* Read data. */
static int
partitioning_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  while (count > 0) {
    const struct region *region = get_region (offset);
    size_t i, len;
    ssize_t r;

    /* Length to end of region. */
    len = region->end - offset + 1;
    if (len > count)
      len = count;

    switch (region->type) {
    case region_file:
      i = region->u.i;
      assert (i < nr_files);
      r = pread (files[i].fd, buf, len, offset - region->start);
      if (r == -1) {
        nbdkit_error ("pread: %s: %m", files[i].filename);
        return -1;
      }
      if (r == 0) {
        nbdkit_error ("pread: %s: unexpected end of file", files[i].filename);
        return -1;
      }
      len = r;
      break;

    case region_data:
      memcpy (buf, &region->u.data[offset - region->start], len);
      break;

    case region_zero:
      memset (buf, 0, len);
      break;
    }

    count -= len;
    buf += len;
    offset += len;
  }

  return 0;
}

/* Write data. */
static int
partitioning_pwrite (void *handle,
                     const void *buf, uint32_t count, uint64_t offset)
{
  while (count > 0) {
    const struct region *region = get_region (offset);
    size_t i, len;
    ssize_t r;

    /* Length to end of region. */
    len = region->end - offset + 1;
    if (len > count)
      len = count;

    switch (region->type) {
    case region_file:
      i = region->u.i;
      assert (i < nr_files);
      r = pwrite (files[i].fd, buf, len, offset - region->start);
      if (r == -1) {
        nbdkit_error ("pwrite: %s: %m", files[i].filename);
        return -1;
      }
      len = r;
      break;

    case region_data:
      /* You can only write same data as already present. */
      if (memcmp (&region->u.data[offset - region->start], buf, len) != 0) {
        nbdkit_error ("attempt to change partition table of virtual disk");
        errno = EIO;
        return -1;
      }
      break;

    case region_zero:
      /* You can only write zeros. */
      if (!is_zero (buf, len)) {
        nbdkit_error ("write non-zeros to padding region");
        errno = EIO;
        return -1;
      }
      break;
    }

    count -= len;
    buf += len;
    offset += len;
  }

  return 0;
}

/* Flush. */
static int
partitioning_flush (void *handle)
{
  size_t i;

  for (i = 0; i < nr_files; ++i) {
    if (fdatasync (files[i].fd) == -1) {
      nbdkit_error ("fdatasync: %m");
      return -1;
    }
  }

  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "partitioning",
  .version           = PACKAGE_VERSION,
  .load              = partitioning_load,
  .unload            = partitioning_unload,
  .config            = partitioning_config,
  .config_complete   = partitioning_config_complete,
  .config_help       = partitioning_config_help,
  .magic_config_key = "file",
  .open              = partitioning_open,
  .get_size          = partitioning_get_size,
  .pread             = partitioning_pread,
  .pwrite            = partitioning_pwrite,
  .flush             = partitioning_flush,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
