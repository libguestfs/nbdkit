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

#ifndef NBDKIT_VIRTUAL_DISK_H
#define NBDKIT_VIRTUAL_DISK_H

#include <stdbool.h>
#include <stdint.h>

#include "regions.h"

extern const char *dir;
extern const char *label;
extern const char *type;
extern int64_t size;
extern bool size_add_estimate;

extern struct random_state random_state;

#define SECTOR_SIZE 512

struct virtual_disk {
  /* Virtual disk layout. */
  struct regions regions;

  /* Disk protective MBR. */
  uint8_t *protective_mbr;

  /* GPT primary partition table header. */
  uint8_t *primary_header;

  /* GPT primary and secondary (backup) PTs.  These are the same. */
  uint8_t *pt;

  /* GPT secondary (backup) PT header. */
  uint8_t *secondary_header;

  /* Size of the filesystem in bytes. */
  uint64_t filesystem_size;

  /* Unique partition GUID. */
  char guid[16];

  /* File descriptor of the temporary file containing the filesystem. */
  int fd;
};

/* virtual-disk.c */
extern void init_virtual_disk (struct virtual_disk *disk)
  __attribute__ ((__nonnull__ (1)));
extern int create_virtual_disk (struct virtual_disk *disk)
  __attribute__ ((__nonnull__ (1)));
extern void free_virtual_disk (struct virtual_disk *disk)
  __attribute__ ((__nonnull__ (1)));

/* partition-gpt.c */
extern int create_partition_table (struct virtual_disk *disk);

/* filesystem.c */
extern int create_filesystem (struct virtual_disk *disk);

#endif /* NBDKIT_VIRTUAL_DISK_H */
