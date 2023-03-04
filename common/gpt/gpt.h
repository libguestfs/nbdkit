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

#ifndef NBDKIT_GPT_H
#define NBDKIT_GPT_H

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
};

#define GPT_SIGNATURE "EFI PART"
#define GPT_REVISION "\0\0\1\0" /* revision 1.0 */

struct gpt_entry {
  char partition_type_guid[16];
  char unique_guid[16];
  uint64_t first_lba;
  uint64_t last_lba;
  uint64_t attributes;
  char name[72];                /* UTF-16LE */
};

/* GPT_MIN_PARTITIONS is the minimum number of partitions and is
 * defined by the UEFI standard (assuming 512 byte sector size).
 *
 * In plugins such as the partitioning plugin, if we are requested to
 * allocate more than GPT_MIN_PARTITIONS then we increase the
 * partition table in chunks of this size.  Note that clients may not
 * support > GPT_MIN_PARTITIONS.
 *
 * GPT_PT_ENTRY_SIZE is the minimum specified by the UEFI spec, but
 * increasing it is not useful.
 */
#define GPT_MIN_PARTITIONS 128
#define GPT_PT_ENTRY_SIZE 128

#endif /* NBDKIT_GPT_H */
