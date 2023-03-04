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

#ifndef NBDKIT_VIRTUAL_FLOPPY_H
#define NBDKIT_VIRTUAL_FLOPPY_H

#include <sys/types.h>
#include <sys/stat.h>

#include "regions.h"
#include "vector.h"

DEFINE_VECTOR_TYPE (idxs, size_t);

struct partition_entry {
  uint8_t bootable;             /* 0x00 or 0x80 if bootable */
  uint8_t chs[3];               /* always set to chs_too_large */
  uint8_t part_type;            /* partition type byte - 0x0C = FAT32 with LBA*/
  uint8_t chs2[3];              /* always set to chs_too_large */
  uint32_t start_sector;        /* 2048 */
  uint32_t num_sectors;
} __attribute__ ((packed));

struct bootsector {
  uint8_t jmp_insn[3];
  uint8_t oem_name[8];                 /* 0x0003 */

  /* BIOS Parameter Block, only required for first sector of FAT. */
  uint16_t bytes_per_sector;           /* 0x000B */
  uint8_t sectors_per_cluster;         /* 0x000D */
  uint16_t reserved_sectors;           /* 0x000E */
  uint8_t nr_fats;                     /* 0x0010 */
  uint16_t nr_root_dir_entries;        /* 0x0011 - always 0 for FAT32 */
  uint16_t old_nr_sectors;             /* 0x0013 - always 0 */
  uint8_t media_descriptor;            /* 0x0015 - always 0xF8 */
  uint16_t old_sectors_per_fat;        /* 0x0016 */
  uint16_t sectors_per_track;          /* 0x0018 - always 0 for LBA */
  uint16_t nr_heads;                   /* 0x001A - always 0 for LBA */
  uint32_t nr_hidden_sectors;          /* 0x001C */
  uint32_t nr_sectors;                 /* 0x0020 */

  /* FAT32 Extended BIOS Parameter Block. */
  uint32_t sectors_per_fat;            /* 0x0024 */
  uint16_t mirroring;                  /* 0x0028 */
  uint16_t fat_version;                /* 0x002A */
  uint32_t root_directory_cluster;     /* 0x002C */
  uint16_t fsinfo_sector;              /* 0x0030 */
  uint16_t backup_bootsect;            /* 0x0032 */
  uint8_t reserved[12];                /* 0x0034 */
  uint8_t physical_drive_number;       /* 0x0040 */
  uint8_t unused;                      /* 0x0041 */
  uint8_t extended_boot_signature;     /* 0x0042 */
  uint32_t volume_id;                  /* 0x0043 */
  uint8_t volume_label[11];            /* 0x0047 */
  uint8_t fstype[8];                   /* 0x0052 - "FAT32   " */

  uint8_t unused2[350];

  /* Partition table.  Not present in first sector of filesystem. */
  uint32_t disk_signature;             /* 0x01B8 */
  uint16_t zero;                       /* 0x01BC - 0x00 0x00 */
  struct partition_entry partition[4]; /* 0x01BE - partition table */

  uint8_t boot_signature[2];           /* 0x01FE - 0x55 0xAA */
} __attribute__ ((packed));

struct fsinfo {
  uint8_t signature[4];         /* 0x52 0x52 0x61 0x41 "RRaA" */
  uint8_t reserved[480];
  uint8_t signature2[4];        /* 0x72 0x72 0x41 0x61 "rrAa" */
  uint32_t free_data_clusters;
  uint32_t last_free_cluster;
  uint8_t reserved2[12];
  uint8_t signature3[4];        /* 0x00 0x00 0x55 0xAA */
} __attribute__ ((packed));

struct file {
  char *name;                   /* Filename. */
  char *host_path;              /* Path of file on the host. */
  struct stat statbuf;          /* stat(2) information, including size. */
  uint32_t first_cluster;       /* First cluster containing this file. */
  uint32_t nr_clusters;         /* Number of clusters. */
};

DEFINE_VECTOR_TYPE (files, struct file);

/* On disk directory entry (non-LFN). */
struct dir_entry {
  uint8_t name[8 + 3];
  uint8_t attributes;           /* 0x0B */
#define DIR_ENTRY_READONLY     0x01
#define DIR_ENTRY_HIDDEN       0x02
#define DIR_ENTRY_SYSTEM       0x04
#define DIR_ENTRY_VOLUME_LABEL 0x08
#define DIR_ENTRY_SUBDIRECTORY 0x10
#define DIR_ENTRY_ARCHIVE      0x20
  uint8_t unused;               /* 0x0C */
  uint8_t ctime_10ms;           /* 0x0D - ctime seconds in 10ms units */
  uint16_t ctime;               /* 0x0E */
  uint16_t cdate;               /* 0x10 */
  uint16_t adate;               /* 0x12 */
  uint16_t cluster_hi;          /* 0x14 - first cluster (high word) */
  uint16_t mtime;               /* 0x16 */
  uint16_t mdate;               /* 0x18 */
  uint16_t cluster_lo;          /* 0x1A - first cluster (low word) */
  uint32_t size;                /* 0x1C - file size */
} __attribute__ ((packed));

DEFINE_VECTOR_TYPE (dir_entries, struct dir_entry);

/* On disk directory entry (LFN). */
struct lfn_entry {
  uint8_t seq;                  /* sequence number */
  uint16_t name1[5];            /* first five UTF-16LE characters */
  uint8_t attributes;           /* 0x0B - always 0x0F */
  uint8_t type;                 /* 0x0C - always 0x00 */
  uint8_t checksum;             /* 0x0D - DOS file name checksum */
  uint16_t name2[6];            /* next six UTF-16LE characters */
  uint16_t cluster_lo;          /* 0x1A - always 0x0000 */
  uint16_t name3[2];            /* last two UTF-16LE characters */
} __attribute__ ((packed));

struct dir {
  size_t pdi;                   /* Link to parent directory (for root, 0). */
  char *name;                   /* Directory name (for root, NULL). */
  struct stat statbuf;          /* stat(2) information (for root, zeroes). */
  uint32_t first_cluster;       /* First cluster containing this dir. */
  uint32_t nr_clusters;         /* Number of clusters. */

  /* List of subdirectories.  This is actually a list of indexes
   * into the floppy->dirs array.
   */
  idxs subdirs;

  /* List of files in this directory.  This is actually a list of
   * indexes into the floppy->files array.
   */
  idxs fileidxs;

  /* On disk directory table. */
  dir_entries table;
};

DEFINE_VECTOR_TYPE (dirs, struct dir);

struct virtual_floppy {
  /* Virtual disk layout. */
  struct regions regions;

  /* Disk MBR. */
  struct bootsector mbr;

  /* Partition boot/first sector (also used for backup copy). */
  struct bootsector bootsect;

  /* Filesystem information sector. */
  struct fsinfo fsinfo;

  /* File Allocation Table (also used for second copy). */
  uint32_t *fat;

  /* All regular files found. */
  files files;

  /* Directories.  dirs[0] == root directory. */
  dirs dirs;

  uint64_t fat_entries;         /* Size of FAT (number of 32 bit entries). */
  uint64_t fat_clusters;        /* Size of FAT (clusters on disk). */
  uint64_t data_size;           /* Size of data region (bytes). */
  uint64_t data_clusters;       /* Size of data region (clusters). */
  uint64_t data_used_clusters;  /* Size of the used part of the data region. */

  /* The disk layout:
   * sector 0:          MBR
   * sector 2048:       partition first sector
   * sector 2049:       filesystem information sector
   * sector 2050-2053:  unused (reserved sectors 2-5)
   * sector 2054:       backup first sector
   * sector 2055-2079:  unused (reserved sectors 7-31)
   * sector 2080:       FAT
   * fat2_start_sector  FAT (second copy)
   * data_start_sector  data region (first cluster is always 2)
   * data_last_sector   last sector of data region
   */
  uint32_t fat2_start_sector;
  uint32_t data_start_sector;
  uint32_t data_last_sector;
};

#define SECTOR_SIZE 512

/* Don't change SECTORS_PER_CLUSTER without also considering the disk
 * layout.  It shouldn't be necessary to change this since this
 * supports the maximum possible disk size, and only wastes virtual
 * space.
 */
#define SECTORS_PER_CLUSTER 32
#define CLUSTER_SIZE (SECTOR_SIZE * SECTORS_PER_CLUSTER)

extern void init_virtual_floppy (struct virtual_floppy *floppy)
  __attribute__ ((__nonnull__ (1)));
extern int create_virtual_floppy (const char *dir, const char *label,
                                  uint64_t size,
                                  struct virtual_floppy *floppy)
  __attribute__ ((__nonnull__ (1, 2, 4)));
extern void free_virtual_floppy (struct virtual_floppy *floppy)
  __attribute__ ((__nonnull__ (1)));
extern int create_directory (size_t di, const char *label,
                             struct virtual_floppy *floppy)
  __attribute__ ((__nonnull__ (2, 3)));
extern int update_directory_first_cluster (size_t di,
                                           struct virtual_floppy *floppy)
  __attribute__ ((__nonnull__ (2)));
extern void pad_string (const char *label, size_t n, uint8_t *out)
  __attribute__ ((__nonnull__ (1, 3)));

#endif /* NBDKIT_VIRTUAL_FLOPPY_H */
