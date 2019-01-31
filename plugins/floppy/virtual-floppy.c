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
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <nbdkit-plugin.h>

#include "get-current-dir-name.h"

#include "byte-swapping.h"
#include "regions.h"
#include "rounding.h"

#include "virtual-floppy.h"

/* This is the Windows 98 OEM name, and some sites recommend using it
 * for greatest compatibility.
 */
#define OEM_NAME "MSWIN4.1"

static ssize_t visit (const char *dir, struct virtual_floppy *floppy);
static int visit_subdirectory (const char *dir, const char *name, const struct stat *statbuf, size_t di, struct virtual_floppy *floppy);
static int visit_file (const char *dir, const char *name, const struct stat *statbuf, size_t di, struct virtual_floppy *floppy);
static int create_mbr (struct virtual_floppy *floppy);
static void chs_too_large (uint8_t *out);
static int create_partition_boot_sector (const char *label, struct virtual_floppy *floppy);
static int create_fsinfo (struct virtual_floppy *floppy);
static int create_fat (struct virtual_floppy *floppy);
static void write_fat_file (uint32_t first_cluster, uint32_t nr_clusters, struct virtual_floppy *floppy);
static int create_regions (struct virtual_floppy *floppy);

void
init_virtual_floppy (struct virtual_floppy *floppy)
{
  memset (floppy, 0, sizeof *floppy);
  init_regions (&floppy->regions);

  /* Assert that the on disk struct sizes are correct. */
  assert (sizeof (struct dir_entry) == 32);
  assert (sizeof (struct lfn_entry) == 32);
  assert (sizeof (struct bootsector) == 512);
  assert (sizeof (struct fsinfo) == 512);
}

int
create_virtual_floppy (const char *dir, const char *label,
                       struct virtual_floppy *floppy)
{
  size_t i;
  uint64_t nr_bytes, nr_clusters;
  uint32_t cluster;

  if (visit (dir, floppy) == -1)
    return -1;

  nbdkit_debug ("floppy: %zu directories and %zu files",
                floppy->nr_dirs, floppy->nr_files);

  /* Create the on disk directory tables. */
  for (i = 0; i < floppy->nr_dirs; ++i) {
    if (create_directory (i, label, floppy) == -1)
      return -1;
  }

  /* We now have a complete list of directories and files, and
   * directories have been converted to on disk directory tables.  So
   * we can assign them to clusters and also precisely calculate the
   * size of the data region and hence the size of the FAT.
   *
   * The first cluster number is always 2 (0 and 1 are reserved), and
   * (in this implementation) always contains the root directory.
   */
  floppy->data_size = 0;
  cluster = 2;
  for (i = 0; i < floppy->nr_dirs; ++i) {
    floppy->dirs[i].first_cluster = cluster;
    nr_bytes =
      ROUND_UP (floppy->dirs[i].table_entries * sizeof (struct dir_entry),
                CLUSTER_SIZE);
    floppy->data_size += nr_bytes;
    nr_clusters = nr_bytes / CLUSTER_SIZE;
    if (cluster + nr_clusters > UINT32_MAX)
      goto too_big;
    floppy->dirs[i].nr_clusters = nr_clusters;
    cluster += nr_clusters;
  }
  for (i = 0; i < floppy->nr_files; ++i) {
    floppy->files[i].first_cluster = cluster;
    nr_bytes = ROUND_UP (floppy->files[i].statbuf.st_size, CLUSTER_SIZE);
    floppy->data_size += nr_bytes;
    nr_clusters = nr_bytes / CLUSTER_SIZE;
    if (cluster + nr_clusters > UINT32_MAX)
      goto too_big;
    floppy->files[i].nr_clusters = nr_clusters;
    cluster += nr_clusters;
  }

  floppy->data_clusters = floppy->data_size / CLUSTER_SIZE;

  /* Despite its name, FAT32 only allows 28 bit cluster numbers, so
   * give an error if we go beyond this.
   */
  if (floppy->data_clusters >= 0x10000000) {
  too_big:
    nbdkit_error ("disk image is too large for the FAT32 disk format");
    return -1;
  }

  nbdkit_debug ("floppy: %" PRIu64 " data clusters, "
                "largest cluster number %" PRIu32 ", "
                "%" PRIu64 " bytes",
                floppy->data_clusters,
                cluster-1,
                floppy->data_size);

  floppy->fat_entries = floppy->data_clusters + 2;
  floppy->fat_clusters = DIV_ROUND_UP (floppy->fat_entries * 4, CLUSTER_SIZE);

  nbdkit_debug ("floppy: %" PRIu64 " FAT entries", floppy->fat_entries);

  /* We can now decide where we will place the FATs and data region on disk. */
  floppy->fat2_start_sector =
    2080 + floppy->fat_clusters * SECTORS_PER_CLUSTER;
  floppy->data_start_sector =
    floppy->fat2_start_sector + floppy->fat_clusters * SECTORS_PER_CLUSTER;
  floppy->data_last_sector =
    floppy->data_start_sector + floppy->data_clusters * SECTORS_PER_CLUSTER - 1;

  /* We now have to go back and update the cluster numbers in the
   * directory entries (which we didn't have available during
   * create_directory above).
   */
  for (i = 0; i < floppy->nr_dirs; ++i) {
    if (update_directory_first_cluster (i, floppy) == -1)
      return -1;
  }

  /* Create MBR. */
  if (create_mbr (floppy) == -1)
    return -1;

  /* Create partition first sector. */
  if (create_partition_boot_sector (label, floppy) == -1)
    return -1;

  /* Create filesystem information sector. */
  if (create_fsinfo (floppy) == -1)
    return -1;

  /* Allocate and populate FAT. */
  if (create_fat (floppy) == -1)
    return -1;

  /* Now we know how large everything is we can create the virtual
   * disk regions.
   */
  if (create_regions (floppy) == -1)
    return -1;

  return 0;
}

void
free_virtual_floppy (struct virtual_floppy *floppy)
{
  size_t i;

  free_regions (&floppy->regions);

  free (floppy->fat);

  for (i = 0; i < floppy->nr_files; ++i) {
    free (floppy->files[i].name);
    free (floppy->files[i].host_path);
  }
  free (floppy->files);

  for (i = 0; i < floppy->nr_dirs; ++i) {
    free (floppy->dirs[i].name);
    free (floppy->dirs[i].subdirs);
    free (floppy->dirs[i].files);
    free (floppy->dirs[i].table);
  }
  free (floppy->dirs);
}

/* Visit files and directories.
 *
 * This constructs the floppy->dirs and floppy->files lists.
 *
 * Returns the directory index, or -1 on error.
 */
static ssize_t
visit (const char *dir, struct virtual_floppy *floppy)
{
  void *np;
  size_t di;
  char *origdir;
  DIR *DIR;
  struct dirent *d;
  int err;
  struct stat statbuf;

  /* Allocate a new index in the directory array.  Note that the root
   * directory will always be at dirs[0].
   */
  di = floppy->nr_dirs;
  np = realloc (floppy->dirs, sizeof (struct dir) * (di+1));
  if (np == NULL) {
    nbdkit_error ("realloc: %m");
    goto error0;
  }
  floppy->dirs = np;
  floppy->nr_dirs++;
  memset (&floppy->dirs[di], 0, sizeof (struct dir));

  /* Because this is called from config_complete, before nbdkit
   * daemonizes or starts any threads, it's safe to use chdir here and
   * greatly simplifies the code.  However we must chdir back to the
   * original directory at the end.
   */
  origdir = get_current_dir_name ();
  if (origdir == NULL) {
    nbdkit_error ("get_current_dir_name: %m");
    goto error0;
  }
  if (chdir (dir) == -1) {
    nbdkit_error ("chdir: %s: %m", dir);
    goto error1;
  }

  DIR = opendir (".");
  if (DIR == NULL) {
    nbdkit_error ("opendir: %s: %m", dir);
    goto error1;
  }

  while (errno = 0, (d = readdir (DIR)) != NULL) {
    if (strcmp (d->d_name, ".") == 0 ||
        strcmp (d->d_name, "..") == 0)
      continue;

    if (lstat (d->d_name, &statbuf) == -1) {
      nbdkit_error ("stat: %s/%s: %m", dir, d->d_name);
      goto error2;
    }

    /* Directory. */
    if (S_ISDIR (statbuf.st_mode)) {
      if (visit_subdirectory (dir, d->d_name, &statbuf, di, floppy) == -1)
        goto error2;
    }
    /* Regular file. */
    else if (S_ISREG (statbuf.st_mode)) {
      if (visit_file (dir, d->d_name, &statbuf, di, floppy) == -1)
        goto error2;
    }
    /* else ALL other file types are ignored - see documentation. */
  }

  /* Did readdir fail? */
  if (errno != 0) {
    nbdkit_error ("readdir: %s: %m", dir);
    goto error2;
  }

  if (closedir (DIR) == -1) {
    nbdkit_error ("closedir: %s: %m", dir);
    goto error1;
  }

  if (chdir (origdir) == -1) {
    nbdkit_error ("chdir: %s: %m", origdir);
    goto error1;
  }
  free (origdir);
  return di;

 error2:
  closedir (DIR);
 error1:
  err = errno;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
  chdir (origdir);
#pragma GCC diagnostic pop
  errno = err;
  free (origdir);
 error0:
  return -1;
}

/* This is called to visit a subdirectory in a directory.  It
 * recursively calls the visit() function, and then adds the
 * subdirectory to the list of subdirectories in the parent.
 */
static int
visit_subdirectory (const char *dir, const char *name,
                    const struct stat *statbuf, size_t di,
                    struct virtual_floppy *floppy)
{
  void *np;
  char *subdir;
  ssize_t sdi;                  /* subdirectory index */
  size_t i;

  if (asprintf (&subdir, "%s/%s", dir, name) == -1) {
    nbdkit_error ("asprintf: %m");
    return -1;
  }
  /* Recursively visit this directory.  As a side effect this adds the
   * new subdirectory to the global list of directories, and returns
   * the index in that list (sdi).
   */
  sdi = visit (subdir, floppy);
  if (sdi == -1) {
    free (subdir);
    return -1;
  }
  free (subdir);

  /* We must set sdi->name because visit() cannot set it. */
  floppy->dirs[sdi].name = strdup (name);
  if (floppy->dirs[sdi].name == NULL) {
    nbdkit_error ("strdup: %m");
    return -1;
  }
  floppy->dirs[sdi].statbuf = *statbuf;
  floppy->dirs[sdi].pdi = di;

  /* Add to the list of subdirs in the parent directory (di). */
  i = floppy->dirs[di].nr_subdirs;
  np = realloc (floppy->dirs[di].subdirs, sizeof (size_t) * (i+1));
  if (np == NULL) {
    nbdkit_error ("realloc: %m");
    return -1;
  }
  floppy->dirs[di].subdirs = np;
  floppy->dirs[di].nr_subdirs++;
  floppy->dirs[di].subdirs[i] = sdi;

  return 0;
}

/* This is called to visit a file in a directory.  It performs some
 * checks and then adds the file to the global list of files, and also
 * adds the file to the list of files in the parent directory.
 */
static int
visit_file (const char *dir, const char *name,
            const struct stat *statbuf, size_t di,
            struct virtual_floppy *floppy)
{
  void *np;
  char *host_path;
  size_t fi, i;

  if (asprintf (&host_path, "%s/%s", dir, name) == -1) {
    nbdkit_error ("asprintf: %m");
    return -1;
  }

  if (statbuf->st_size >= UINT32_MAX) {
    nbdkit_error ("%s: file is larger than maximum supported by VFAT",
                  host_path);
    free (host_path);
    return -1;
  }

  /* Add to global list of files. */
  fi = floppy->nr_files;
  np = realloc (floppy->files, sizeof (struct file) * (fi+1));
  if (np == NULL) {
    nbdkit_error ("realloc: %m");
    free (host_path);
    return -1;
  }
  floppy->files = np;
  floppy->nr_files++;
  floppy->files[fi].name = strdup (name);
  if (floppy->files[fi].name == NULL) {
    nbdkit_error ("strdup: %m");
    free (host_path);
    return -1;
  }
  floppy->files[fi].host_path = host_path;
  floppy->files[fi].statbuf = *statbuf;

  /* Add to the list of files in the parent directory (di). */
  i = floppy->dirs[di].nr_files;
  np = realloc (floppy->dirs[di].files, sizeof (size_t) * (i+1));
  if (np == NULL) {
    nbdkit_error ("realloc: %m");
    return -1;
  }
  floppy->dirs[di].files = np;
  floppy->dirs[di].nr_files++;
  floppy->dirs[di].files[i] = fi;

  return 0;
}

/* Create the Master Boot Record sector of the disk. */
static int
create_mbr (struct virtual_floppy *floppy)
{
  uint32_t num_sectors;
  uint64_t last_sector;

  /* The last sector number in the partition. */
  last_sector =
    floppy->data_start_sector
    + floppy->data_clusters * SECTORS_PER_CLUSTER
    - 1;

  nbdkit_debug ("floppy: last sector %" PRIu64, last_sector);

  if (last_sector >= UINT32_MAX) {
    nbdkit_error ("disk image is too large for the MBR disk format");
    return -1;
  }
  num_sectors = last_sector - 2048 + 1;

  memcpy (floppy->mbr.oem_name, OEM_NAME, sizeof floppy->mbr.oem_name);

  /* We could choose a random disk signature, but it seems safer to
   * leave the field zero.
   */
  floppy->mbr.disk_signature = htole32 (0);
  floppy->mbr.boot_signature[0] = 0x55;
  floppy->mbr.boot_signature[1] = 0xAA;

  /* Only one partition. */
  floppy->mbr.partition[0].bootable = 0;
  chs_too_large (floppy->mbr.partition[0].chs);
  floppy->mbr.partition[0].part_type = 0x0c;
  chs_too_large (floppy->mbr.partition[0].chs2);
  floppy->mbr.partition[0].start_sector = htole32 (2048);
  floppy->mbr.partition[0].num_sectors = htole32 (num_sectors);

  return 0;
}

static void
chs_too_large (uint8_t *out)
{
  const int c = 1023, h = 254, s = 63;

  out[0] = h;
  out[1] = (c & 0x300) >> 2 | s;
  out[2] = c & 0xff;
}

static int
create_partition_boot_sector (const char *label, struct virtual_floppy *floppy)
{
  memcpy (floppy->bootsect.oem_name, OEM_NAME,
          sizeof floppy->bootsect.oem_name);

  floppy->bootsect.bytes_per_sector = htole16 (SECTOR_SIZE);
  floppy->bootsect.sectors_per_cluster = SECTORS_PER_CLUSTER;
  floppy->bootsect.reserved_sectors = htole16 (32);
  floppy->bootsect.nr_fats = 2;
  floppy->bootsect.nr_root_dir_entries = htole16 (0);
  floppy->bootsect.old_nr_sectors = htole16 (0);
  floppy->bootsect.media_descriptor = 0xf8;
  floppy->bootsect.old_sectors_per_fat = htole16 (0);
  floppy->bootsect.sectors_per_track = htole16 (0);
  floppy->bootsect.nr_heads = htole16 (0);
  floppy->bootsect.nr_hidden_sectors = htole32 (0);
  floppy->bootsect.nr_sectors = htole32 (floppy->data_last_sector + 1);

  floppy->bootsect.sectors_per_fat =
    htole32 (floppy->fat_clusters * SECTORS_PER_CLUSTER);
  floppy->bootsect.mirroring = htole16 (0);
  floppy->bootsect.fat_version = htole16 (0);
  floppy->bootsect.root_directory_cluster = htole32 (2);
  floppy->bootsect.fsinfo_sector = htole16 (1);
  floppy->bootsect.backup_bootsect = htole16 (6);
  floppy->bootsect.physical_drive_number = 0;
  floppy->bootsect.extended_boot_signature = 0x29;
  /* The volume ID should be generated based on the filesystem
   * creation date and time, but the old qemu VVFAT driver just used a
   * fixed number here.
   */
  floppy->bootsect.volume_id = htole32 (0x01020304);
  pad_string (label, 11, floppy->bootsect.volume_label);
  memcpy (floppy->bootsect.fstype, "FAT32   ", 8);

  floppy->bootsect.boot_signature[0] = 0x55;
  floppy->bootsect.boot_signature[1] = 0xAA;

  return 0;
}

static int
create_fsinfo (struct virtual_floppy *floppy)
{
  floppy->fsinfo.signature[0] = 0x52; /* "RRaA" */
  floppy->fsinfo.signature[1] = 0x52;
  floppy->fsinfo.signature[2] = 0x61;
  floppy->fsinfo.signature[3] = 0x41;
  floppy->fsinfo.signature2[0] = 0x72; /* "rrAa" */
  floppy->fsinfo.signature2[1] = 0x72;
  floppy->fsinfo.signature2[2] = 0x41;
  floppy->fsinfo.signature2[3] = 0x61;
  floppy->fsinfo.free_data_clusters = htole32 (0);
  floppy->fsinfo.last_free_cluster = htole32 (2 + floppy->data_clusters);
  floppy->fsinfo.signature3[0] = 0x00;
  floppy->fsinfo.signature3[1] = 0x00;
  floppy->fsinfo.signature3[2] = 0x55;
  floppy->fsinfo.signature3[3] = 0xAA;
  return 0;
}

/* Allocate and populate the File Allocation Table. */
static int
create_fat (struct virtual_floppy *floppy)
{
  size_t i;

  /* Note there is only one copy held in memory.  The two FAT
   * regions in the virtual disk point to the same copy.
   */
  floppy->fat = calloc (floppy->fat_entries, 4);
  if (floppy->fat == NULL) {
    nbdkit_error ("calloc: %m");
    return -1;
  }

  /* Populate the FAT.  First two entries are reserved and
   * contain standard data.
   */
  floppy->fat[0] = htole32 (0x0ffffff8);
  floppy->fat[1] = htole32 (0x0fffffff);

  for (i = 0; i < floppy->nr_dirs; ++i) {
    write_fat_file (floppy->dirs[i].first_cluster,
                    floppy->dirs[i].nr_clusters, floppy);
  }
  for (i = 0; i < floppy->nr_files; ++i) {
    write_fat_file (floppy->files[i].first_cluster,
                    floppy->files[i].nr_clusters, floppy);
  }

  return 0;
}

static void
write_fat_file (uint32_t first_cluster, uint32_t nr_clusters,
                struct virtual_floppy *floppy)
{
  uint32_t cl;

  /* It's possible for files to have zero size.  These don't occupy
   * any space in the disk or FAT so we just skip them here.
   */
  if (nr_clusters == 0)
    return;

  for (cl = 0; cl < nr_clusters - 1; ++cl) {
    assert (first_cluster + cl < floppy->fat_entries);
    /* Directories and files are stored contiguously so the entry in
     * the FAT always points to the next cluster (except for the
     * last one, handled below).
     */
    floppy->fat[first_cluster + cl] = htole32 (first_cluster + cl + 1);
  }

  /* Last cluster / end of file marker. */
  floppy->fat[first_cluster + cl] = htole32 (0x0fffffff);
}

/* Lay out the final virtual disk. */
static int
create_regions (struct virtual_floppy *floppy)
{
  struct region region;
  size_t i;

  /* MBR. */
  region.start = 0;
  region.end = SECTOR_SIZE-1;
  region.len = region.end - region.start + 1;
  region.type = region_data;
  region.u.data = (void *) &floppy->mbr;
  region.description = "MBR";
  if (append_one_region (&floppy->regions, region) == -1)
    return -1;

  /* Free space before first partition. */
  region.start = SECTOR_SIZE;
  region.end = 2048*SECTOR_SIZE-1;
  region.len = region.end - region.start + 1;
  region.type = region_zero;
  region.description = "first partition alignment";
  if (append_one_region (&floppy->regions, region) == -1)
    return -1;

  /* Partition boot sector. */
  region.start = 2048*SECTOR_SIZE;
  region.end = 2049*SECTOR_SIZE-1;
  region.len = region.end - region.start + 1;
  region.type = region_data;
  region.u.data = (void *) &floppy->bootsect;
  region.description = "partition boot sector";
  if (append_one_region (&floppy->regions, region) == -1)
    return -1;

  /* Filesystem information sector. */
  region.start = 2049*SECTOR_SIZE;
  region.end = 2050*SECTOR_SIZE-1;
  region.len = region.end - region.start + 1;
  region.type = region_data;
  region.u.data = (void *) &floppy->fsinfo;
  region.description = "filesystem information sector";
  if (append_one_region (&floppy->regions, region) == -1)
    return -1;

  /* Free space (reserved sectors 2-5). */
  region.start = 2050*SECTOR_SIZE;
  region.end = 2054*SECTOR_SIZE-1;
  region.len = region.end - region.start + 1;
  region.type = region_zero;
  region.description = "reserved sectors 2-5";
  if (append_one_region (&floppy->regions, region) == -1)
    return -1;

  /* Backup boot sector. */
  region.start = 2054*SECTOR_SIZE;
  region.end = 2055*SECTOR_SIZE-1;
  region.len = region.end - region.start + 1;
  region.type = region_data;
  region.u.data = (void *) &floppy->bootsect;
  region.description = "backup boot sector";
  if (append_one_region (&floppy->regions, region) == -1)
    return -1;

  /* Free space (reserved sectors 7-31). */
  region.start = 2055*SECTOR_SIZE;
  region.end = 2080*SECTOR_SIZE-1;
  region.len = region.end - region.start + 1;
  region.type = region_zero;
  region.description = "reserved sectors 7-31";
  if (append_one_region (&floppy->regions, region) == -1)
    return -1;

  /* First copy of FAT. */
  region.start = 2080*SECTOR_SIZE;
  region.len = floppy->fat_entries*4;
  region.end = region.start + region.len - 1;
  region.type = region_data;
  region.u.data = (void *) floppy->fat;
  region.description = "FAT #1";
  if (append_one_region (&floppy->regions, region) == -1)
    return -1;

  /* Free space after FAT (optional). */
  region.start = region.end + 1;
  region.end = floppy->fat2_start_sector*SECTOR_SIZE - 1;
  region.len = region.end - region.start + 1;
  region.type = region_zero;
  region.description = "FAT #2 alignment";
  if (region.len > 0 && append_one_region (&floppy->regions, region) == -1)
    return -1;

  /* Second copy of FAT. */
  region.start = floppy->fat2_start_sector*SECTOR_SIZE;
  region.len = floppy->fat_entries*4;
  region.end = region.start + region.len - 1;
  region.type = region_data;
  region.u.data = (void *) floppy->fat;
  region.description = "FAT #2";
  if (append_one_region (&floppy->regions, region) == -1)
    return -1;

  /* Free space after FAT (optional). */
  region.start = region.end + 1;
  region.end = floppy->data_start_sector*SECTOR_SIZE - 1;
  region.len = region.end - region.start + 1;
  region.type = region_zero;
  region.description = "data region alignment";
  if (region.len > 0 && append_one_region (&floppy->regions, region) == -1)
    return -1;

  /* Now we're into the data region.  We add all directory tables
   * first.
   */
  for (i = 0; i < floppy->nr_dirs; ++i) {
    /* Directories can never be completely empty because of the volume
     * label (root) or "." and ".." entries (non-root).
     */
    assert (floppy->dirs[i].table_entries > 0);

    region.start = region.end + 1;
    region.len = floppy->dirs[i].table_entries * sizeof (struct dir_entry);
    region.end = region.start + region.len - 1;
    region.type = region_data;
    region.u.data = (void *) floppy->dirs[i].table;
    region.description = i == 0 ? "root directory" : "directory";
    if (append_one_region (&floppy->regions, region) == -1)
      return -1;

    /* Optional free space to align to end of cluster. */
    region.start = region.end + 1;
    region.end = ROUND_UP (region.start, CLUSTER_SIZE) - 1;
    region.len = region.end - region.start + 1;
    region.type = region_zero;
    region.description =
      i == 0 ? "root directory padding" : "directory padding";
    if (region.len > 0 && append_one_region (&floppy->regions, region) == -1)
      return -1;
  }

  /* Add all files. */
  for (i = 0; i < floppy->nr_files; ++i) {
    /* It's possible for a file to have zero size, in which case it
     * doesn't occupy a region or cluster.
     */
    if (floppy->files[i].statbuf.st_size == 0)
      continue;

    region.start = region.end + 1;
    region.len = floppy->files[i].statbuf.st_size;
    region.end = region.start + region.len - 1;
    region.type = region_file;
    region.u.i = i;
    region.description = "file";
    if (append_one_region (&floppy->regions, region) == -1)
      return -1;

    /* Optional free space to align to end of cluster. */
    region.start = region.end + 1;
    region.end = ROUND_UP (region.start, CLUSTER_SIZE) - 1;
    region.len = region.end - region.start + 1;
    region.type = region_zero;
    region.description = "file padding";
    if (region.len > 0 && append_one_region (&floppy->regions, region) == -1)
      return -1;
  }

  nbdkit_debug ("floppy: %zu regions, "
                "total disk size %" PRIi64,
                nr_regions (&floppy->regions),
                virtual_size (&floppy->regions));

  return 0;
}
