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
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
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

#include "random.h"
#include "regions.h"
#include "virtual-disk.h"

#ifndef HAVE_FDATASYNC
#define fdatasync fsync
#endif

/* Debug flag: -D partitioning.regions=1: Print the regions table. */
int partitioning_debug_regions;

/* alignment, mbr_id, type_guid set on the command line for
 * following partitions.
 */
unsigned long alignment = DEFAULT_ALIGNMENT;
int mbr_id = DEFAULT_MBR_ID;
char type_guid[16]; /* initialized by partitioning_load function below */

/* partition-type parameter. */
int parttype = PARTTYPE_UNSET;

/* Files supplied on the command line. */
struct file *files = NULL;
size_t nr_files = 0;

/* Virtual disk layout. */
struct regions regions;

/* Primary and secondary partition tables and extended boot records.
 * Secondary PT is only used for GPT.  EBR array of sectors is only
 * used for MBR with > 4 partitions and has length equal to nr_files-3.
 */
unsigned char *primary = NULL, *secondary = NULL, **ebr = NULL;

/* Used to generate random unique partition GUIDs for GPT. */
static struct random_state random_state;

static void
partitioning_load (void)
{
  init_regions (&regions);
  parse_guid (DEFAULT_TYPE_GUID, type_guid);
  xsrandom (time (NULL), &random_state);
}

static void
partitioning_unload (void)
{
  size_t i;

  for (i = 0; i < nr_files; ++i)
    close (files[i].fd);
  free (files);

  /* We don't need to free regions.regions[].u.data because it points
   * to primary, secondary or ebr which we free here.
   */
  free_regions (&regions);

  free (primary);
  free (secondary);
  if (ebr) {
    for (i = 0; i < nr_files-3; ++i)
      free (ebr[i]);
    free (ebr);
  }
}

static int
partitioning_config (const char *key, const char *value)
{
  struct file *p;
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
      file.guid[i] = xrandom (&random_state) & 0xff;

    p = realloc (files, (nr_files+1) * sizeof (struct file));
    if (p == NULL) {
      err = errno;
      close (file.fd);
      errno = err;
      nbdkit_error ("realloc: %m");
      return -1;
    }
    files = p;
    files[nr_files] = file;
    nr_files++;
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
    int64_t r;

    r = nbdkit_parse_size (value);
    if (r == -1)
      return -1;

    if (!(r >= SECTOR_SIZE && r <= MAX_ALIGNMENT)) {
      nbdkit_error ("partition alignment %" PRIi64 " should be "
                    ">= sector size %" PRIu64 " and "
                    "<= maximum alignment %" PRIu64,
                    r, SECTOR_SIZE, MAX_ALIGNMENT);
      return -1;
    }
    if (!IS_ALIGNED (r, SECTOR_SIZE)) {
      nbdkit_error ("partition alignment %" PRIi64 " should be "
                    "a multiple of sector size %" PRIu64,
                    r, SECTOR_SIZE);
      return -1;
    }

    alignment = r;
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
  bool needs_gpt;

  /* Not enough / too many files? */
  if (nr_files == 0) {
    nbdkit_error ("at least one file= parameter must be supplied");
    return -1;
  }

  total_size = 0;
  for (i = 0; i < nr_files; ++i)
    total_size += files[i].statbuf.st_size;
  needs_gpt = total_size > MAX_MBR_DISK_SIZE;

  /* Choose default parttype if not set. */
  if (parttype == PARTTYPE_UNSET) {
    if (needs_gpt || nr_files > 4) {
      parttype = PARTTYPE_GPT;
      nbdkit_debug ("picking partition type GPT");
    }
    else {
      parttype = PARTTYPE_MBR;
      nbdkit_debug ("picking partition type MBR");
    }
  }
  else if (parttype == PARTTYPE_MBR && needs_gpt) {
    nbdkit_error ("MBR partition table type supports "
                  "a maximum virtual disk size of about 2 TB, "
                  "but you requested %zu partition(s) "
                  "and a total size of %" PRIu64 " bytes (> %" PRIu64 ").  "
                  "Try using: partition-type=gpt",
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
  return NBDKIT_HANDLE_NOT_NEEDED;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Get the disk size. */
static int64_t
partitioning_get_size (void *handle)
{
  return virtual_size (&regions);
}

/* Serves the same data over multiple connections. */
static int
partitioning_can_multi_conn (void *handle)
{
  return 1;
}

/* Read data. */
static int
partitioning_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  while (count > 0) {
    const struct region *region = find_region (&regions, offset);
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
    const struct region *region = find_region (&regions, offset);
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
  .can_multi_conn    = partitioning_can_multi_conn,
  .pread             = partitioning_pread,
  .pwrite            = partitioning_pwrite,
  .flush             = partitioning_flush,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
