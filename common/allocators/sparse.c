/* nbdkit
 * Copyright (C) 2017-2020 Red Hat Inc.
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

#include <pthread.h>

#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "iszero.h"
#include "vector.h"

#include "allocator.h"
#include "allocator-internal.h"

/* This allocator implements a sparse array of any size up to 2⁶³-1
 * bytes.
 *
 * The array reads as zeroes until something is written.
 *
 * The implementation aims to be reasonably efficient for ordinary
 * sized disks, while permitting huge (but sparse) disks for testing.
 * Everything allocated has to be stored in memory.  There is no
 * temporary file backing.
 *
 * XXX It would be nice to change the locking to use r/w locks here,
 * as that ought to greatly benefit read-heavy loads using multi-conn.
 */

/* Two level directory for the sparse array.
 *
 * nbdkit supports disk sizes up to 2⁶³-1.  The aim of the sparse
 * array is to support up to 63 bit images for testing, although it
 * won't necessarily be efficient for that use.  However it should
 * also be efficient for more reasonable sized disks.
 *
 * Although the CPU implements effectively the same kind of data
 * structure (page tables) there are some advantages of reimplementing
 * this:
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
 * (offset, pointer to L2 directory).
 *
 * Updating the L1 directory requires a linear scan but that operation
 * should be very rare.  Because the L1 directory is stored in order
 * of offset, we can use an efficient binary search for lookups.
 *
 * Each L1 directory entry can address up to PAGE_SIZE*L2_SIZE bytes
 * in the virtual disk image.  With the current parameters this is
 * 128MB, which is enough for a 100MB image to fit into a single L1
 * directory, or a 10GB image to fit into 80 L1 entries.  The page
 * pointers in the L2 directory can be NULL (meaning no page / all
 * zeroes).
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

struct l2_entry {
  void *page;                   /* Pointer to page (array of PAGE_SIZE bytes).*/
};

struct l1_entry {
  uint64_t offset;              /* Virtual offset of this entry. */
  struct l2_entry *l2_dir;      /* Pointer to L2 directory (L2_SIZE entries). */
};

DEFINE_VECTOR_TYPE(l1_dir, struct l1_entry);

struct sparse_array {
  struct allocator a;           /* Must come first. */
  pthread_mutex_t lock;
  l1_dir l1_dir;                /* L1 directory. */
};

/* Free L1 and/or L2 directories. */
static void
free_l2_dir (struct l2_entry *l2_dir)
{
  size_t i;

  for (i = 0; i < L2_SIZE; ++i)
    free (l2_dir[i].page);
  free (l2_dir);
}

static void
sparse_array_free (struct allocator *a)
{
  struct sparse_array *sa = (struct sparse_array *) a;
  size_t i;

  if (sa) {
    for (i = 0; i < sa->l1_dir.len; ++i)
      free_l2_dir (sa->l1_dir.ptr[i].l2_dir);
    free (sa->l1_dir.ptr);
    pthread_mutex_destroy (&sa->lock);
    free (sa);
  }
}

static int
sparse_array_set_size_hint (struct allocator *a, uint64_t size)
{
  /* Ignored. */
  return 0;
}

/* Comparison function used when searching through the L1 directory. */
static int
compare_l1_offsets (const void *offsetp, const struct l1_entry *e)
{
  const uint64_t offset = *(uint64_t *)offsetp;

  if (offset < e->offset) return -1;
  if (offset >= e->offset + PAGE_SIZE*L2_SIZE) return 1;
  return 0;
}

/* Insert an entry in the L1 directory, keeping it ordered by offset.
 * This involves an expensive linear scan but should be very rare.
 */
static int
insert_l1_entry (struct sparse_array *sa, const struct l1_entry *entry)
{
  size_t i;

  for (i = 0; i < sa->l1_dir.len; ++i) {
    if (entry->offset < sa->l1_dir.ptr[i].offset) {
      /* Insert new entry before i'th directory entry. */
      if (l1_dir_insert (&sa->l1_dir, *entry, i) == -1) {
        nbdkit_error ("realloc: %m");
        return -1;
      }
      if (sa->a.debug)
        nbdkit_debug ("%s: inserted new L1 entry for %" PRIu64
                      " at l1_dir.ptr[%zu]",
                      __func__, entry->offset, i);
      return 0;
    }

    /* This should never happens since each entry in the the L1
     * directory is supposed to be unique.
     */
    assert (entry->offset != sa->l1_dir.ptr[i].offset);
  }

  /* Insert new entry at the end. */
  if (l1_dir_append (&sa->l1_dir, *entry) == -1) {
    nbdkit_error ("realloc: %m");
    return -1;
  }
  if (sa->a.debug)
    nbdkit_debug ("%s: inserted new L1 entry for %" PRIu64
                  " at end of l1_dir", __func__, entry->offset);
  return 0;
}

/* Look up a virtual offset, returning the address of the offset, the
 * count of bytes to the end of the page, and a pointer to the L2
 * directory entry containing the page pointer.
 *
 * If the create flag is set then a new page and/or directory will be
 * allocated if necessary.  Use this flag when writing.
 *
 * NULL may be returned normally if the page is not mapped (meaning it
 * reads as zero).  However if the create flag is set and NULL is
 * returned, this indicates an error.
 */
static void *
lookup (struct sparse_array *sa, uint64_t offset, bool create,
        uint64_t *remaining, struct l2_entry **l2_entry)
{
  struct l1_entry *entry;
  struct l2_entry *l2_dir;
  uint64_t o;
  void *page;
  struct l1_entry new_entry;

  *remaining = PAGE_SIZE - (offset & (PAGE_SIZE-1));

 again:
  /* Search the L1 directory. */
  entry = l1_dir_search (&sa->l1_dir, &offset, compare_l1_offsets);

  if (sa->a.debug) {
    if (entry)
      nbdkit_debug ("%s: search L1 dir: entry found: offset %" PRIu64,
                    __func__, entry->offset);
    else
      nbdkit_debug ("%s: search L1 dir: no entry found", __func__);
  }

  if (entry) {
    l2_dir = entry->l2_dir;

    /* Which page in the L2 directory? */
    o = (offset - entry->offset) / PAGE_SIZE;
    if (l2_entry)
      *l2_entry = &l2_dir[o];
    page = l2_dir[o].page;
    if (!page && create) {
      /* No page allocated.  Allocate one if creating. */
      page = calloc (PAGE_SIZE, 1);
      if (page == NULL) {
        nbdkit_error ("calloc: %m");
        return NULL;
      }
      l2_dir[o].page = page;
    }
    if (!page)
      return NULL;
    else
      return page + (offset & (PAGE_SIZE-1));
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
  new_entry.l2_dir = calloc (L2_SIZE, sizeof (struct l2_entry));
  if (new_entry.l2_dir == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }
  if (insert_l1_entry (sa, &new_entry) == -1) {
    free (new_entry.l2_dir);
    return NULL;
  }
  goto again;
}

static int
sparse_array_read (struct allocator *a,
                   void *buf, uint64_t count, uint64_t offset)
{
  struct sparse_array *sa = (struct sparse_array *) a;
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&sa->lock);
  uint64_t n;
  void *p;

  while (count > 0) {
    p = lookup (sa, offset, false, &n, NULL);
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

static int
sparse_array_write (struct allocator *a,
                    const void *buf, uint64_t count, uint64_t offset)
{
  struct sparse_array *sa = (struct sparse_array *) a;
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&sa->lock);
  uint64_t n;
  void *p;

  while (count > 0) {
    p = lookup (sa, offset, true, &n, NULL);
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

static int sparse_array_zero (struct allocator *a,
                              uint64_t count, uint64_t offset);

static int
sparse_array_fill (struct allocator *a, char c,
                   uint64_t count, uint64_t offset)
{
  struct sparse_array *sa = (struct sparse_array *) a;
  uint64_t n;
  void *p;

  if (c == 0)
    return sparse_array_zero (a, count, offset);

  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&sa->lock);

  while (count > 0) {
    p = lookup (sa, offset, true, &n, NULL);
    if (p == NULL)
      return -1;

    if (n > count)
      n = count;
    memset (p, c, n);

    count -= n;
    offset += n;
  }

  return 0;
}

static int
sparse_array_zero (struct allocator *a, uint64_t count, uint64_t offset)
{
  struct sparse_array *sa = (struct sparse_array *) a;
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&sa->lock);
  uint64_t n;
  void *p;
  struct l2_entry *l2_entry;

  while (count > 0) {
    p = lookup (sa, offset, false, &n, &l2_entry);
    if (n > count)
      n = count;

    if (p) {
      if (n < PAGE_SIZE)
        memset (p, 0, n);
      else
        assert (p == l2_entry->page);

      /* If the whole page is now zero, free it. */
      if (n >= PAGE_SIZE || is_zero (l2_entry->page, PAGE_SIZE)) {
        if (sa->a.debug)
          nbdkit_debug ("%s: freeing zero page at offset %" PRIu64,
                        __func__, offset);
        free (l2_entry->page);
        l2_entry->page = NULL;
      }
    }

    count -= n;
    offset += n;
  }

  return 0;
}

static int
sparse_array_blit (struct allocator *a1,
                   struct allocator *a2,
                   uint64_t count,
                   uint64_t offset1, uint64_t offset2)
{
  struct sparse_array *sa2 = (struct sparse_array *) a2;
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&sa2->lock);
  uint64_t n;
  void *p;
  struct l2_entry *l2_entry;

  assert (a1 != a2);
  assert (strcmp (a2->f->type, "sparse") == 0);

  while (count > 0) {
    p = lookup (sa2, offset2, true, &n, &l2_entry);
    if (p == NULL)
      return -1;

    if (n > count)
      n = count;

    /* Read the source allocator (a1) directly to p which points into
     * the right place in sa2.
     */
    if (a1->f->read (a1, p, n, offset1) == -1)
      return -1;

    /* If the whole page is now zero, free it. */
    if (is_zero (l2_entry->page, PAGE_SIZE)) {
      if (sa2->a.debug)
        nbdkit_debug ("%s: freeing zero page at offset %" PRIu64,
                      __func__, offset2);
      free (l2_entry->page);
      l2_entry->page = NULL;
    }

    count -= n;
    offset1 += n;
    offset2 += n;
  }

  return 0;
}

static int
sparse_array_extents (struct allocator *a,
                      uint64_t count, uint64_t offset,
                      struct nbdkit_extents *extents)
{
  struct sparse_array *sa = (struct sparse_array *) a;
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&sa->lock);
  uint64_t n;
  uint32_t type;
  void *p;

  while (count > 0) {
    p = lookup (sa, offset, false, &n, NULL);

    /* Work out the type of this extent. */
    if (p == NULL)
      /* No backing page, so it's a hole. */
      type = NBDKIT_EXTENT_HOLE | NBDKIT_EXTENT_ZERO;
    else {
      if (is_zero (p, n))
        /* A backing page and it's all zero, it's a zero extent. */
        type = NBDKIT_EXTENT_ZERO;
      else
        /* Normal allocated data. */
        type = 0;
    }
    if (nbdkit_add_extent (extents, offset, n, type) == -1)
      return -1;

    if (n > count)
      n = count;

    count -= n;
    offset += n;
  }

  return 0;
}

static struct allocator *
sparse_array_create (const void *paramsv)
{
  const allocator_parameters *params  = paramsv;
  struct sparse_array *sa;

  if (params->len > 0) {
    nbdkit_error ("allocator=sparse does not take extra parameters");
    return NULL;
  }

  sa = calloc (1, sizeof *sa);
  if (sa == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }
  pthread_mutex_init (&sa->lock, NULL);

  return (struct allocator *) sa;
}

static struct allocator_functions functions = {
  .type = "sparse",
  .create = sparse_array_create,
  .free = sparse_array_free,
  .set_size_hint = sparse_array_set_size_hint,
  .read = sparse_array_read,
  .write = sparse_array_write,
  .fill = sparse_array_fill,
  .zero = sparse_array_zero,
  .blit = sparse_array_blit,
  .extents = sparse_array_extents,
};

static void register_sparse_array (void) __attribute__((constructor));

static void
register_sparse_array (void)
{
  register_allocator (&functions);
}
