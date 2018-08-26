/* nbdkit
 * Copyright (C) 2017-2018 Red Hat Inc.
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
#include <errno.h>
#include <assert.h>

#include <nbdkit-plugin.h>

/* The size of disk in bytes (initialized by size=<SIZE> parameter). */
static int64_t size = 0;

/* Two level directory.
 *
 * nbdkit supports sizes up to 2⁶³-1 so we need (up to) a 63 bit
 * address space.  The aim of the plugin is to support up to 63 bit
 * images for testing, although it won't necessarily be efficient for
 * that use.  However it should also be efficient for more reasonable
 * sized disks.
 *
 * Although the CPU implements effectively the same kind of data
 * structure (page tables) there are some advantages of reimplementing
 * this in the plugin:
 *
 * 1. Support for 32 bit (or even 64 bit since the virtual memory
 * address space on 64 bit machines is not 63 bits in size).
 *
 * 2. In Linux, overcommit defaults prevent use of virtual memory as a
 * sparse array without intrusive system configuration changes.
 *
 * 3. Could choose a page size which is more appropriate for disk
 * images, plus some architectures have much larger page sizes than
 * others making behaviour inconsistent across arches.
 *
 * To achieve this we use a B-Tree-like structure.  The L1 directory
 * contains an ordered, non-overlapping, non-contiguous list of
 * (offset, pointer to L2 directory).  Updating the L1 directory
 * requires a linear scan but that operation should be very rare.
 * Note the page pointers in the L2 directory can be NULL (meaning no
 * page / all zeroes).
 *
 * Each L1 directory entry can address up to PAGE_SIZE*L2_SIZE bytes
 * in the virtual disk image.  With the current parameters this is
 * 128MB, which is enough for a 100MB image to fit into a single L1
 * directory, or a 10GB image to fit into 80 L1 entries.
 *
 * ┌────────────────────┐
 * │ L1 directory       │       ┌────────────────────┐
 * │ offset, entry 0 ─────────▶ | L2 directory       |
 * │ offset, entry 1    │       | page 0          ─────────▶ page
 * │ offset, entry 2    │       │ page 1          ─────────▶ page
 * │ ...                │       │ page 2          ─────────▶ page
 * └────────────────────┘       │ ...                │
 *                              │ page L2_SIZE-1  ─────────▶ page
 *                              └────────────────────┘
 */
#define PAGE_SIZE 32768
#define L2_SIZE   4096

struct l1_entry {
  uint64_t offset;              /* Virtual offset of this entry. */
  void **l2_dir;                /* Pointer to L2 directory. */
};

static struct l1_entry *l1_dir; /* L1 directory. */
static size_t l1_size;          /* Number of entries in L1 directory. */

/* Debug directory operations (-D memory.dir=1). */
int memory_debug_dir;

/* Free L1 and/or L2 directories. */
static void
free_l2_dir (void **l2_dir)
{
  size_t i;

  for (i = 0; i < L2_SIZE; ++i)
    free (l2_dir[i]);
  free (l2_dir);
}

static void
free_l1_dir (void)
{
  size_t i;

  for (i = 0; i < l1_size; ++i)
    free_l2_dir (l1_dir[i].l2_dir);
  free (l1_dir);
  l1_dir = NULL;
  l1_size = 0;
}

/* Comparison function used when searching through the L1 directory. */
static int
compare_l1_offsets (const void *offsetp, const void *entryp)
{
  const uint64_t offset = *(uint64_t *)offsetp;
  const struct l1_entry *e = entryp;

  if (offset < e->offset) return -1;
  if (offset >= e->offset + PAGE_SIZE*L2_SIZE) return 1;
  return 0;
}

/* Insert an entry in the L1 directory, keeping it ordered by offset.
 * This involves an expensive linear scan but should be very rare.
 */
static int
insert_l1_entry (const struct l1_entry *entry)
{
  size_t i;
  struct l1_entry *old_l1_dir = l1_dir;

  /* The l1_dir always ends up one bigger, so reallocate it first. */
  l1_dir = realloc (l1_dir, (l1_size+1) * sizeof (struct l1_entry));
  if (l1_dir == NULL) {
    l1_dir = old_l1_dir;
    nbdkit_error ("realloc");
    return -1;
  }

  for (i = 0; i < l1_size; ++i) {
    if (entry->offset < l1_dir[i].offset) {
      /* Insert new entry before i'th directory entry. */
      memmove (&l1_dir[i+1], &l1_dir[i],
               (l1_size-i) * sizeof (struct l1_entry));
      l1_dir[i] = *entry;
      l1_size++;
      if (memory_debug_dir)
        nbdkit_debug ("%s: inserted new L1 entry for %" PRIu64
                      " at l1_dir[%zu]",
                      __func__, entry->offset, i);
      return 0;
    }

    /* This should never happens since each entry in the the L1
     * directory is supposed to be unique.
     */
    assert (entry->offset != l1_dir[i].offset);
  }

  /* Insert new entry at the end. */
  l1_dir[l1_size] = *entry;
  l1_size++;
  if (memory_debug_dir)
    nbdkit_debug ("%s: inserted new L1 entry for %" PRIu64
                  " at end of l1_dir", __func__, entry->offset);
  return 0;
}

/* Look up a virtual address, returning the page containing the
 * virtual address and the count of bytes to the end of the page.
 *
 * If the create flag is set then a new page and/or directory will be
 * allocated if necessary.  Use this flag when writing.
 *
 * NULL may be returned normally if the page is not mapped (meaning it
 * reads as zero).  However if the create flag is set and NULL is
 * returned, this indicates an error.
 */
static void *
lookup (uint64_t offset, bool create, uint32_t *count)
{
  struct l1_entry *entry;
  void **l2_dir;
  uint64_t o;
  void *page;
  struct l1_entry new_entry;

  *count = PAGE_SIZE - (offset % PAGE_SIZE);

 again:
  /* Search the L1 directory. */
  entry = bsearch (&offset, l1_dir, l1_size, sizeof (struct l1_entry),
                   compare_l1_offsets);

  if (memory_debug_dir) {
    if (entry)
      nbdkit_debug ("%s: search L1 dir: entry found: offset %" PRIu64,
                    __func__, entry->offset);
    else
      nbdkit_debug ("%s: search L1 dir: no entry found", __func__);
  }

  if (entry) {
    l2_dir = entry->l2_dir;

    /* Which page in the L2 directory? */
    o = offset - entry->offset;
    page = l2_dir[o / PAGE_SIZE];
    if (!page && create) {
      /* No page allocated.  Allocate one if creating. */
      page = calloc (PAGE_SIZE, 1);
      if (page == NULL) {
        nbdkit_error ("calloc");
        return NULL;
      }
      l2_dir[o / PAGE_SIZE] = page;
    }
    if (!page)
      return NULL;
    else
      return page + (o % PAGE_SIZE);
  }

  /* No L1 directory entry found. */
  if (!create)
    return NULL;

  /* No L1 directory entry, and we're creating, so we need to allocate
   * a new L1 directory entry and insert it in the L1 directory, and
   * allocate the L2 directory with NULL page pointers.  Then we can
   * repeat the above search to create the page.
   */
  new_entry.offset = offset & ~(PAGE_SIZE*L2_SIZE-1);
  new_entry.l2_dir = calloc (L2_SIZE, sizeof (void *));
  if (new_entry.l2_dir == NULL) {
    nbdkit_error ("calloc");
    return NULL;
  }
  if (insert_l1_entry (&new_entry) == -1) {
    free (new_entry.l2_dir);
    return NULL;
  }
  goto again;
}

static void
memory_unload (void)
{
  free_l1_dir ();
}

static int
memory_config (const char *key, const char *value)
{
  int64_t r;

  if (strcmp (key, "size") == 0) {
    r = nbdkit_parse_size (value);
    if (r == -1)
      return -1;
    if (r > SIZE_MAX) {
      nbdkit_error ("size > SIZE_MAX");
      return -1;
    }
    size = (ssize_t) r;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int
memory_config_complete (void)
{
  if (size == 0) {
    nbdkit_error ("you must specify size=<SIZE> on the command line");
    return -1;
  }
  return 0;
}

#define memory_config_help \
  "size=<SIZE>  (required) Size of the backing disk"

/* Create the per-connection handle. */
static void *
memory_open (int readonly)
{
  /* Used only as a handle pointer. */
  static int mh;

  return &mh;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

/* Get the disk size. */
static int64_t
memory_get_size (void *handle)
{
  return (int64_t) size;
}

/* Read data. */
static int
memory_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  uint32_t n;
  void *p;

  while (count > 0) {
    p = lookup (offset, false, &n);
    if (n > count)
      n = count;

    if (p == NULL)
      memset (buf, 0, n);
    else
      memcpy (buf, p, n);

    buf += n;
    count -= n;
    offset += n;
  }

  return 0;
}

/* Write data. */
static int
memory_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset)
{
  uint32_t n;
  void *p;

  while (count > 0) {
    p = lookup (offset, true, &n);
    if (p == NULL)
      return -1;

    if (n > count)
      n = count;
    memcpy (p, buf, n);

    buf += n;
    count -= n;
    offset += n;
  }

  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "memory",
  .version           = PACKAGE_VERSION,
  .unload            = memory_unload,
  .config            = memory_config,
  .config_complete   = memory_config_complete,
  .config_help       = memory_config_help,
  .open              = memory_open,
  .get_size          = memory_get_size,
  .pread             = memory_pread,
  .pwrite            = memory_pwrite,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
