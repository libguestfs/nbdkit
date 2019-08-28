/* nbdkit
 * Copyright (C) 2018-2019 Red Hat Inc.
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

#include <nbdkit-filter.h>

#include "byte-swapping.h"

#include "partition.h"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

int partnum = -1;

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

static int
partition_prepare (struct nbdkit_next_ops *next_ops, void *nxdata,
                   void *handle)
{
  struct handle *h = handle;
  int64_t size;
  uint8_t lba01[2*SECTOR_SIZE]; /* LBA 0 and 1 */
  int r;
  int err;

  size = next_ops->get_size (nxdata);
  if (size == -1)
    return -1;
  if (size < 2 * SECTOR_SIZE) {
    nbdkit_error ("disk is too small to be a partitioned disk");
    return -1;
  }

  nbdkit_debug ("disk size=%" PRIi64, size);

  if (next_ops->pread (nxdata, lba01, sizeof lba01, 0, 0, &err) == -1)
    return -1;

  /* Is it GPT? */
  if (size >= 2 * 34 * SECTOR_SIZE &&
      memcmp (&lba01[SECTOR_SIZE], "EFI PART", 8) == 0)
    r = find_gpt_partition (next_ops, nxdata, size, &lba01[SECTOR_SIZE],
                            &h->offset, &h->range);
  /* Is it MBR? */
  else if (lba01[0x1fe] == 0x55 && lba01[0x1ff] == 0xAA)
    r = find_mbr_partition (next_ops, nxdata, size, lba01,
                            &h->offset, &h->range);
  else {
    nbdkit_error ("disk does not contain MBR or GPT partition table signature");
    r = -1;
  }
  if (r == -1)
    return -1;

  /* The find_*_partition functions set h->offset & h->range to the
   * partition boundaries.  We additionally check that they are inside
   * the underlying disk.
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

/* Extents. */
static int
partition_extents (struct nbdkit_next_ops *next_ops, void *nxdata,
                   void *handle, uint32_t count, uint64_t offs, uint32_t flags,
                   struct nbdkit_extents *extents, int *err)
{
  struct handle *h = handle;
  size_t i;
  struct nbdkit_extents *extents2;
  struct nbdkit_extent e;

  extents2 = nbdkit_extents_new (offs + h->offset, h->offset + h->range);
  if (extents2 == NULL) {
    *err = errno;
    return -1;
  }
  if (next_ops->extents (nxdata, count, offs + h->offset,
                         flags, extents2, err) == -1)
    goto error;

  for (i = 0; i < nbdkit_extents_count (extents2); ++i) {
    e = nbdkit_get_extent (extents2, i);
    e.offset -= h->offset;
    if (nbdkit_add_extent (extents, e.offset, e.length, e.type) == -1)
      goto error;
  }
  nbdkit_extents_free (extents2);
  return 0;

 error:
  nbdkit_extents_free (extents2);
  return -1;
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
  .extents           = partition_extents,
};

NBDKIT_REGISTER_FILTER(filter)
