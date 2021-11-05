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

#ifdef HAVE_LIBZSTD

#include <zstd.h>

/* This is derived from the sparse array implementation - see
 * common/allocators/sparse.c for details of how it works.
 *
 * TO DO:
 *
 * (1) We can avoid decompressing a page if we know we are going to
 * write over / trim / zero the whole page.
 *
 * (2) Locking is correct but very naive.  It should be possible to
 * take much more fine-grained locks.
 *
 * (3) Better stats: Can we iterate over the page table in order to
 * find the ratio of uncompressed : compressed?
 *
 * Once some optimizations are made it would be worth profiling to
 * find the hot spots.
 */
#define PAGE_SIZE 32768
#define L2_SIZE   4096

struct l2_entry {
  void *page;                   /* Pointer to compressed data. */
};

struct l1_entry {
  uint64_t offset;              /* Virtual offset of this entry. */
  struct l2_entry *l2_dir;      /* Pointer to L2 directory (L2_SIZE entries). */
};

DEFINE_VECTOR_TYPE(l1_dir, struct l1_entry);

struct zstd_array {
  struct allocator a;           /* Must come first. */
  pthread_mutex_t lock;
  l1_dir l1_dir;                /* L1 directory. */

  /* Compression context and decompression stream.  We use the
   * streaming API for decompression because it allows us to
   * decompress without storing the compressed size, so we need a
   * streaming object.  But in fact decompression context and stream
   * are the same thing since zstd 1.3.0.
   *
   * If we ever get serious about making this allocator work well
   * multi-threaded [at the moment the locking is too course-grained],
   * then the zstd documentation recommends creating a context per
   * thread.
   */
  ZSTD_CCtx *zcctx;
  ZSTD_DStream *zdstrm;

  /* Collect stats when we compress a page. */
  uint64_t stats_uncompressed_bytes;
  uint64_t stats_compressed_bytes;
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
zstd_array_free (struct allocator *a)
{
  struct zstd_array *za = (struct zstd_array *) a;
  size_t i;

  if (za) {
    if (za->stats_compressed_bytes > 0)
      nbdkit_debug ("zstd: compression ratio: %g : 1",
                    (double) za->stats_uncompressed_bytes /
                    za->stats_compressed_bytes);

    ZSTD_freeCCtx (za->zcctx);
    ZSTD_freeDStream (za->zdstrm);
    for (i = 0; i < za->l1_dir.len; ++i)
      free_l2_dir (za->l1_dir.ptr[i].l2_dir);
    free (za->l1_dir.ptr);
    pthread_mutex_destroy (&za->lock);
    free (za);
  }
}

static int
zstd_array_set_size_hint (struct allocator *a, uint64_t size)
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
insert_l1_entry (struct zstd_array *za, const struct l1_entry *entry)
{
  size_t i;

  for (i = 0; i < za->l1_dir.len; ++i) {
    if (entry->offset < za->l1_dir.ptr[i].offset) {
      /* Insert new entry before i'th directory entry. */
      if (l1_dir_insert (&za->l1_dir, *entry, i) == -1) {
        nbdkit_error ("realloc: %m");
        return -1;
      }
      if (za->a.debug)
        nbdkit_debug ("%s: inserted new L1 entry for %" PRIu64
                      " at l1_dir.ptr[%zu]",
                      __func__, entry->offset, i);
      return 0;
    }

    /* This should never happens since each entry in the the L1
     * directory is supposed to be unique.
     */
    assert (entry->offset != za->l1_dir.ptr[i].offset);
  }

  /* Insert new entry at the end. */
  if (l1_dir_append (&za->l1_dir, *entry) == -1) {
    nbdkit_error ("realloc: %m");
    return -1;
  }
  if (za->a.debug)
    nbdkit_debug ("%s: inserted new L1 entry for %" PRIu64
                  " at end of l1_dir", __func__, entry->offset);
  return 0;
}

/* Look up a virtual offset.
 *
 * If the L2 page is mapped then this uncompresses the page into the
 * caller's buffer (of size PAGE_SIZE), returning the address of the
 * offset, the count of bytes to the end of the page, and a pointer to
 * the L2 directory entry containing the page pointer.
 *
 * If the L2 page is not mapped this clears the caller's buffer, also
 * returning the pointer.
 *
 * To read data you don't need to do anything else.
 *
 * To write data, after updating the buffer, you must subsequently
 * call compress() below.
 *
 * This function cannot return an error.
 */
static void *
lookup_decompress (struct zstd_array *za, uint64_t offset, void *buf,
                   uint64_t *remaining, struct l2_entry **l2_entry)
{
  struct l1_entry *entry;
  struct l2_entry *l2_dir;
  uint64_t o;
  void *page;

  *remaining = PAGE_SIZE - (offset & (PAGE_SIZE-1));

  /* Search the L1 directory. */
  entry = l1_dir_search (&za->l1_dir, &offset, compare_l1_offsets);

  if (za->a.debug) {
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

    if (page) {
      /* Decompress the page into the user buffer.  We assume this can
       * never fail since the only pages we decompress are ones we
       * have compressed.  We use the streaming API because the normal
       * ZSTD_decompressDCtx function requires the compressed size,
       * whereas the streaming API does not.
       */
      ZSTD_inBuffer inb = { .src = page, .size = SIZE_MAX, .pos = 0 };
      ZSTD_outBuffer outb = { .dst = buf, .size = PAGE_SIZE, .pos = 0 };

      ZSTD_initDStream (za->zdstrm);
      while (outb.pos < outb.size)
        ZSTD_decompressStream (za->zdstrm, &outb, &inb);
      assert (outb.pos == PAGE_SIZE);
    }
    else
      memset (buf, 0, PAGE_SIZE);

    return buf + (offset & (PAGE_SIZE-1));
  }

  /* No L1 directory entry found. */
  memset (buf, 0, PAGE_SIZE);
  return buf + (offset & (PAGE_SIZE-1));
}

/* Compress a page back after modifying it.
 *
 * This replaces a L2 page with a new version compressed from the
 * modified user buffer.
 *
 * It may fail, calling nbdkit_error and returning -1.
 */
static int
compress (struct zstd_array *za, uint64_t offset, void *buf)
{
  struct l1_entry *entry;
  struct l2_entry *l2_dir;
  uint64_t o;
  void *page;
  struct l1_entry new_entry;
  size_t n;

 again:
  /* Search the L1 directory. */
  entry = l1_dir_search (&za->l1_dir, &offset, compare_l1_offsets);

  if (za->a.debug) {
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
    free (l2_dir[o].page);
    l2_dir[o].page = NULL;

    /* Allocate a new page. */
    n = ZSTD_compressBound (PAGE_SIZE);
    page = malloc (n);
    if (page == NULL) {
      nbdkit_error ("malloc: %m");
      return -1;
    }
    n = ZSTD_compressCCtx (za->zcctx, page, n,
                           buf, PAGE_SIZE, ZSTD_CLEVEL_DEFAULT);
    if (ZSTD_isError (n)) {
      nbdkit_error ("ZSTD_compressCCtx: %s", ZSTD_getErrorName (n));
      return -1;
    }
    page = realloc (page, n);
    assert (page != NULL);
    l2_dir[o].page = page;
    za->stats_uncompressed_bytes += PAGE_SIZE;
    za->stats_compressed_bytes += n;
    return 0;
  }

  /* No L1 directory entry, so we need to allocate a new L1 directory
   * entry and insert it in the L1 directory, and allocate the L2
   * directory with NULL page pointers.  Then we can repeat the above
   * search to create the page.
   */
  new_entry.offset = offset & ~(PAGE_SIZE*L2_SIZE-1);
  new_entry.l2_dir = calloc (L2_SIZE, sizeof (struct l2_entry));
  if (new_entry.l2_dir == NULL) {
    nbdkit_error ("calloc: %m");
    return -1;
  }
  if (insert_l1_entry (za, &new_entry) == -1) {
    free (new_entry.l2_dir);
    return -1;
  }
  goto again;
}

static int
zstd_array_read (struct allocator *a,
                 void *buf, uint64_t count, uint64_t offset)
{
  struct zstd_array *za = (struct zstd_array *) a;
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&za->lock);
  CLEANUP_FREE void *tbuf = NULL;
  uint64_t n;
  void *p;

  tbuf = malloc (PAGE_SIZE);
  if (tbuf == NULL) {
    nbdkit_error ("malloc: %m");
    return -1;
  }

  while (count > 0) {
    p = lookup_decompress (za, offset, tbuf, &n, NULL);
    if (n > count)
      n = count;

    memcpy (buf, p, n);

    buf += n;
    count -= n;
    offset += n;
  }

  return 0;
}

static int
zstd_array_write (struct allocator *a,
                  const void *buf, uint64_t count, uint64_t offset)
{
  struct zstd_array *za = (struct zstd_array *) a;
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&za->lock);
  CLEANUP_FREE void *tbuf = NULL;
  uint64_t n;
  void *p;

  tbuf = malloc (PAGE_SIZE);
  if (tbuf == NULL) {
    nbdkit_error ("malloc: %m");
    return -1;
  }

  while (count > 0) {
    p = lookup_decompress (za, offset, tbuf, &n, NULL);

    if (n > count)
      n = count;
    memcpy (p, buf, n);

    if (compress (za, offset, tbuf) == -1)
      return -1;

    buf += n;
    count -= n;
    offset += n;
  }

  return 0;
}

static int zstd_array_zero (struct allocator *a,
                            uint64_t count, uint64_t offset);

static int
zstd_array_fill (struct allocator *a, char c,
                   uint64_t count, uint64_t offset)
{
  struct zstd_array *za = (struct zstd_array *) a;
  CLEANUP_FREE void *tbuf = NULL;
  uint64_t n;
  void *p;

  if (c == 0) {
    zstd_array_zero (a, count, offset);
    return 0;
  }

  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&za->lock);

  tbuf = malloc (PAGE_SIZE);
  if (tbuf == NULL) {
    nbdkit_error ("malloc: %m");
    return -1;
  }

  while (count > 0) {
    p = lookup_decompress (za, offset, tbuf, &n, NULL);

    if (n > count)
      n = count;
    memset (p, c, n);

    if (compress (za, offset, tbuf) == -1)
      return -1;

    count -= n;
    offset += n;
  }

  return 0;
}

static int
zstd_array_zero (struct allocator *a, uint64_t count, uint64_t offset)
{
  struct zstd_array *za = (struct zstd_array *) a;
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&za->lock);
  CLEANUP_FREE void *tbuf = NULL;
  uint64_t n;
  void *p;
  struct l2_entry *l2_entry = NULL;

  tbuf = malloc (PAGE_SIZE);
  if (tbuf == NULL) {
    nbdkit_error ("malloc: %m");
    return -1;
  }

  while (count > 0) {
    p = lookup_decompress (za, offset, tbuf, &n, &l2_entry);

    if (n > count)
      n = count;
    memset (p, 0, n);

    if (l2_entry && l2_entry->page) {
      /* If the whole page is now zero, free it. */
      if (n >= PAGE_SIZE || is_zero (l2_entry->page, PAGE_SIZE)) {
        if (za->a.debug)
          nbdkit_debug ("%s: freeing zero page at offset %" PRIu64,
                        __func__, offset);
        free (l2_entry->page);
        l2_entry->page = NULL;
      }
      else {
        if (compress (za, offset, tbuf) == -1)
          return -1;
      }
    }

    count -= n;
    offset += n;
  }

  return 0;
}

static int
zstd_array_blit (struct allocator *a1,
                 struct allocator *a2,
                 uint64_t count,
                 uint64_t offset1, uint64_t offset2)
{
  struct zstd_array *za2 = (struct zstd_array *) a2;
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&za2->lock);
  CLEANUP_FREE void *tbuf = NULL;
  uint64_t n;
  void *p;

  assert (a1 != a2);
  assert (strcmp (a2->f->type, "zstd") == 0);

  tbuf = malloc (PAGE_SIZE);
  if (tbuf == NULL) {
    nbdkit_error ("malloc: %m");
    return -1;
  }

  while (count > 0) {
    p = lookup_decompress (za2, offset2, tbuf, &n, NULL);

    if (n > count)
      n = count;

    /* Read the source allocator (a1) directly to p which points into
     * the right place in za2.
     */
    if (a1->f->read (a1, p, n, offset1) == -1)
      return -1;

    if (compress (za2, offset2, tbuf) == -1)
      return -1;

    count -= n;
    offset1 += n;
    offset2 += n;
  }

  return 0;
}

static int
zstd_array_extents (struct allocator *a,
                      uint64_t count, uint64_t offset,
                      struct nbdkit_extents *extents)
{
  struct zstd_array *za = (struct zstd_array *) a;
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&za->lock);
  CLEANUP_FREE void *buf = NULL;
  uint64_t n;
  uint32_t type;
  void *p;
  struct l2_entry *l2_entry;

  buf = malloc (PAGE_SIZE);
  if (buf == NULL) {
    nbdkit_error ("malloc: %m");
    return -1;
  }

  while (count > 0) {
    p = lookup_decompress (za, offset, buf, &n, &l2_entry);

    /* Work out the type of this extent. */
    if (l2_entry->page == NULL)
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

struct allocator *
zstd_array_create (const void *paramsv)
{
  const allocator_parameters *params  = paramsv;
  struct zstd_array *za;

  if (params->len > 0) {
    nbdkit_error ("allocator=zstd does not take extra parameters");
    return NULL;
  }

  za = calloc (1, sizeof *za);
  if (za == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }

  pthread_mutex_init (&za->lock, NULL);

  za->zcctx = ZSTD_createCCtx ();
  if (za->zcctx == NULL) {
    nbdkit_error ("ZSTD_createCCtx: %m");
    free (za);
    return NULL;
  }
  za->zdstrm = ZSTD_createDStream ();
  if (za->zdstrm == NULL) {
    nbdkit_error ("ZSTD_createDStream: %m");
    ZSTD_freeCCtx (za->zcctx);
    free (za);
    return NULL;
  }

  za->stats_uncompressed_bytes = za->stats_compressed_bytes = 0;

  return (struct allocator *) za;
}

static struct allocator_functions functions = {
  .type = "zstd",
  .create = zstd_array_create,
  .free = zstd_array_free,
  .set_size_hint = zstd_array_set_size_hint,
  .read = zstd_array_read,
  .write = zstd_array_write,
  .fill = zstd_array_fill,
  .zero = zstd_array_zero,
  .blit = zstd_array_blit,
  .extents = zstd_array_extents,
};

static void register_zstd_array (void) __attribute__((constructor));

static void
register_zstd_array (void)
{
  register_allocator (&functions);
}

#endif /* !HAVE_LIBZSTD */
