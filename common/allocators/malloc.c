/* nbdkit
 * Copyright (C) 2013-2020 Red Hat Inc.
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
#include <string.h>
#include <sys/mman.h>

#include <pthread.h>

#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "vector.h"

#include "allocator.h"
#include "allocator-internal.h"

/* This allocator implements a direct-mapped non-sparse RAM disk using
 * malloc, with optional mlock.
 */

DEFINE_VECTOR_TYPE(bytearray, uint8_t);

struct m_alloc {
  struct allocator a;           /* Must come first. */
  bool use_mlock;

  /* Byte array (vector) implementing the direct-mapped disk.  Note we
   * don't use the .size field.  Accesses must be protected by the
   * lock since writes may try to extend the array.
   */
  pthread_rwlock_t lock;
  bytearray ba;
};

static void
m_alloc_free (struct allocator *a)
{
  struct m_alloc *ma = (struct m_alloc *) a;

  if (ma) {
    free (ma->ba.ptr);
    pthread_rwlock_destroy (&ma->lock);
    free (ma);
  }
}

/* Extend the underlying bytearray if needed.  mlock if requested. */
static int
extend (struct m_alloc *ma, uint64_t new_size)
{
  ACQUIRE_WRLOCK_FOR_CURRENT_SCOPE (&ma->lock);
  size_t old_size, n;

  if (ma->ba.alloc < new_size) {
    old_size = ma->ba.alloc;
    n = new_size - ma->ba.alloc;

    /* Since the memory might be moved by realloc, we must unlock the
     * original array.
     */
    if (ma->use_mlock)
      munlock (ma->ba.ptr, ma->ba.alloc);

    if (bytearray_reserve (&ma->ba, n) == -1) {
      nbdkit_error ("realloc: %m");
      return -1;
    }

    /* Hints to the kernel.  Doesn't matter if these fail.
     * XXX Consider in future: MADV_MERGEABLE (tunable)
     */
#ifdef MADV_RANDOM
    madvise (ma->ba.ptr, ma->ba.alloc, MADV_RANDOM);
#endif
#ifdef MADV_WILLNEED
    madvise (ma->ba.ptr, ma->ba.alloc, MADV_WILLNEED);
#endif
#ifdef MADV_DONTFORK
    madvise (ma->ba.ptr, ma->ba.alloc, MADV_DONTFORK);
#endif
#ifdef MADV_HUGEPAGE
    madvise (ma->ba.ptr, ma->ba.alloc, MADV_HUGEPAGE);
#endif
#ifdef MADV_DONTDUMP
    madvise (ma->ba.ptr, ma->ba.alloc, MADV_DONTDUMP);
#endif

    /* Initialize the newly allocated memory to 0. */
    memset (ma->ba.ptr + old_size, 0, n);

    if (ma->use_mlock) {
      if (mlock (ma->ba.ptr, ma->ba.alloc) == -1) {
        nbdkit_error ("allocator=malloc: mlock: %m");
        return -1;
      }
    }
  }

  return 0;
}

static int
m_alloc_set_size_hint (struct allocator *a, uint64_t size_hint)
{
  struct m_alloc *ma = (struct m_alloc *) a;
  return extend (ma, size_hint);
}

static void
m_alloc_read (struct allocator *a, void *buf,
              uint32_t count, uint64_t offset)
{
  struct m_alloc *ma = (struct m_alloc *) a;
  ACQUIRE_RDLOCK_FOR_CURRENT_SCOPE (&ma->lock);

  /* Avoid reading beyond the end of the allocated array.  Return
   * zeroes for that part.
   */
  if (offset >= ma->ba.alloc)
    memset (buf, 0, count);
  else if (offset + count > ma->ba.alloc) {
    memcpy (buf, ma->ba.ptr + offset, ma->ba.alloc - offset);
    memset (buf + ma->ba.alloc - offset, 0, offset + count - ma->ba.alloc);
  }
  else
    memcpy (buf, ma->ba.ptr + offset, count);
}

static int
m_alloc_write (struct allocator *a, const void *buf,
               uint32_t count, uint64_t offset)
{
  struct m_alloc *ma = (struct m_alloc *) a;

  if (extend (ma, offset+count) == -1)
    return -1;

  /* This is correct: Even though we are writing, we only need to
   * acquire the read lock here.  The write lock applies to changing
   * the metadata and it was acquired if we called extend().
   */
  ACQUIRE_RDLOCK_FOR_CURRENT_SCOPE (&ma->lock);
  memcpy (ma->ba.ptr + offset, buf, count);
  return 0;
}

static int
m_alloc_fill (struct allocator *a, char c, uint32_t count, uint64_t offset)
{
  struct m_alloc *ma = (struct m_alloc *) a;

  if (extend (ma, offset+count) == -1)
    return -1;

  /* See comment in m_alloc_write. */
  ACQUIRE_RDLOCK_FOR_CURRENT_SCOPE (&ma->lock);
  memset (ma->ba.ptr + offset, 0, count);
  return 0;
}

static void
m_alloc_zero (struct allocator *a, uint32_t count, uint64_t offset)
{
  struct m_alloc *ma = (struct m_alloc *) a;
  ACQUIRE_RDLOCK_FOR_CURRENT_SCOPE (&ma->lock);

  /* Try to avoid extending the array, since the unallocated part
   * always reads as zero.
   */
  if (offset < ma->ba.alloc) {
    if (offset + count > ma->ba.alloc)
      memset (ma->ba.ptr + offset, 0, ma->ba.alloc - offset);
    else
      memset (ma->ba.ptr + offset, 0, count);
  }
}

static int
m_alloc_blit (struct allocator *a1, struct allocator *a2,
              uint32_t count, uint64_t offset1, uint64_t offset2)
{
  struct m_alloc *ma2 = (struct m_alloc *) a2;

  if (extend (ma2, offset2+count) == -1)
    return -1;

  /* See comment in m_alloc_write. */
  ACQUIRE_RDLOCK_FOR_CURRENT_SCOPE (&ma2->lock);
  a1->read (a1, ma2->ba.ptr + offset2, count, offset1);
  return 0;
}

static int
m_alloc_extents (struct allocator *a,
                 uint32_t count, uint64_t offset,
                 struct nbdkit_extents *extents)
{
  /* Always fully allocated.  XXX In theory we could detect zeroes
   * quite quickly and return that information, allowing the client to
   * avoid reads.  However we'd probably want to store a bitmap of
   * which sectors we are known to have written to, and that
   * complicates the implementation quite a lot.
   */
  return nbdkit_add_extent (extents, offset, count, 0);
}

static struct allocator functions = {
  .free = m_alloc_free,
  .set_size_hint = m_alloc_set_size_hint,
  .read = m_alloc_read,
  .write = m_alloc_write,
  .fill = m_alloc_fill,
  .zero = m_alloc_zero,
  .blit = m_alloc_blit,
  .extents = m_alloc_extents,
};

struct allocator *
create_malloc (bool use_mlock)
{
  struct m_alloc *ma;

  ma = calloc (1, sizeof *ma);
  if (ma == NULL) {
    nbdkit_error ("calloc");
    return NULL;
  }
  ma->a = functions;
  ma->use_mlock = use_mlock;
  pthread_rwlock_init (&ma->lock, NULL);
  ma->ba = (bytearray) empty_vector;
  return (struct allocator *) ma;
}
