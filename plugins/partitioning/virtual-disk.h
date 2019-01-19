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

#ifndef NBDKIT_VIRTUAL_DISK_H
#define NBDKIT_VIRTUAL_DISK_H

#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "rounding.h"

#include "regions.h"

#define SECTOR_SIZE UINT64_C(512)

/* Maximum size of MBR disks.  This is an approximation based on the
 * known limit (2^32 sectors) and an estimate based on the amount of
 * padding between partitions.
 */
#define MAX_MBR_DISK_SIZE (UINT32_MAX * SECTOR_SIZE - 5 * MAX_ALIGNMENT)

/* GPT_MIN_PARTITIONS is the minimum number of partitions and is
 * defined by the UEFI standard (assuming 512 byte sector size).  If
 * we are requested to allocate more than GPT_MIN_PARTITIONS then we
 * increase the partition table in chunks of this size.  Note that
 * clients may not support > GPT_MIN_PARTITIONS.
 *
 * GPT_PT_ENTRY_SIZE is the minimum specified by the UEFI spec, but
 * increasing it is not useful.
 */
#define GPT_MIN_PARTITIONS 128
#define GPT_PT_ENTRY_SIZE 128

/* For GPT, the number of entries in the partition table array (PTA),
 * and the number of LBAs which the PTA occupies.  The latter will be
 * 32 if the number of files is <= GPT_MIN_PARTITIONS, which is the
 * normal case.
 */
#define GPT_PTA_SIZE ROUND_UP (nr_files, GPT_MIN_PARTITIONS)
#define GPT_PTA_LBAs (GPT_PTA_SIZE * GPT_PT_ENTRY_SIZE / SECTOR_SIZE)

/* Maximum possible and default alignment between partitions. */
#define MAX_ALIGNMENT (2048 * SECTOR_SIZE)
#define DEFAULT_ALIGNMENT MAX_ALIGNMENT

/* Default MBR partition ID and GPT partition type GUID. */
#define DEFAULT_MBR_ID 0x83
#define DEFAULT_TYPE_GUID "0FC63DAF-8483-4772-8E79-3D69D8477DE4"

extern int partitioning_debug_regions;

extern unsigned long alignment;
extern int mbr_id;
extern char type_guid[16];

#define PARTTYPE_UNSET 0
#define PARTTYPE_MBR   1
#define PARTTYPE_GPT   2
extern int parttype;

/* A file supplied on the command line. */
struct file {
  const char *filename;         /* file= supplied on the command line */
  int fd;
  struct stat statbuf;
  char guid[16];                /* random GUID used for GPT */
  unsigned long alignment;      /* alignment of this partition */
  int mbr_id;                   /* MBR ID of this partition */
  char type_guid[16];           /* partition type GUID of this partition */
};

extern struct file *files;
extern size_t nr_files;

extern struct regions regions;
extern unsigned char *primary, *secondary, **ebr;

/* Main entry point called after files array has been populated. */
extern int create_virtual_disk_layout (void);

/* Parse a GPT GUID.  Note that GPT GUIDs have peculiar
 * characteristics which make them unlike general GUIDs.
 */
extern int parse_guid (const char *str, char *out)
  __attribute__((__nonnull__ (1, 2)));

/* Internal function for creating a single MBR PTE.  The GPT code
 * calls this for creating the protective MBR.
 */
extern void create_mbr_partition_table_entry (const struct region *,
                                              bool bootable, int partition_id,
                                              unsigned char *)
  __attribute__((__nonnull__ (1, 4)));

/* Create MBR or GPT layout. */
extern void create_mbr_layout (void);
extern void create_gpt_layout (void);

#endif /* NBDKIT_VIRTUAL_DISK_H */
