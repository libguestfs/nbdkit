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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <assert.h>

#include "cleanup.h"
#include "isaligned.h"
#include "minmax.h"
#include "rounding.h"
#include "vector.h"

#include "internal.h"

/* Cap nr_extents to avoid sending over-large replies to the client,
 * and to avoid a plugin with frequent alternations consuming too much
 * memory.
 */
#define MAX_EXTENTS (1 * 1024 * 1024)

/* Appendable list of extents. */
DEFINE_VECTOR_TYPE (extents, struct nbdkit_extent);

struct nbdkit_extents {
  extents extents;

  uint64_t start, end; /* end is one byte beyond the end of the range */

  /* Where we expect the next extent to be added.  If
   * nbdkit_add_extent has never been called this is -1.  Note this
   * field is updated even if we don't actually add the extent because
   * it's used to check for API violations.
   */
  int64_t next;
};

NBDKIT_DLL_PUBLIC struct nbdkit_extents *
nbdkit_extents_new (uint64_t start, uint64_t end)
{
  struct nbdkit_extents *r;

  if (start > INT64_MAX || end > INT64_MAX) {
    nbdkit_error ("nbdkit_extents_new: "
                  "start (%" PRIu64 ") or end (%" PRIu64 ") > INT64_MAX",
                  start, end);
    errno = ERANGE;
    return NULL;
  }

  /* 0-length ranges are possible, so start == end is not an error. */
  if (start > end) {
    nbdkit_error ("nbdkit_extents_new: "
                  "start (%" PRIu64 ") >= end (%" PRIu64 ")",
                  start, end);
    errno = ERANGE;
    return NULL;
  }

  r = malloc (sizeof *r);
  if (r == NULL) {
    nbdkit_error ("nbdkit_extents_new: malloc: %m");
    return NULL;
  }
  r->extents = (extents) empty_vector;
  r->start = start;
  r->end = end;
  r->next = -1;
  return r;
}

NBDKIT_DLL_PUBLIC void
nbdkit_extents_free (struct nbdkit_extents *exts)
{
  if (exts) {
    free (exts->extents.ptr);
    free (exts);
  }
}

NBDKIT_DLL_PUBLIC size_t
nbdkit_extents_count (const struct nbdkit_extents *exts)
{
  return exts->extents.len;
}

NBDKIT_DLL_PUBLIC struct nbdkit_extent
nbdkit_get_extent (const struct nbdkit_extents *exts, size_t i)
{
  assert (i < exts->extents.len);
  return exts->extents.ptr[i];
}

/* Insert *e in the list at the end. */
static int
append_extent (struct nbdkit_extents *exts, const struct nbdkit_extent *e)
{
  if (extents_append (&exts->extents, *e) == -1) {
    nbdkit_error ("nbdkit_add_extent: realloc: %m");
    return -1;
  }

  return 0;
}

NBDKIT_DLL_PUBLIC int
nbdkit_add_extent (struct nbdkit_extents *exts,
                   uint64_t offset, uint64_t length, uint32_t type)
{
  uint64_t overlap;

  /* Extents must be added in strictly ascending, contiguous order. */
  if (exts->next >= 0 && exts->next != offset) {
    nbdkit_error ("nbdkit_add_extent: "
                  "extents must be added in ascending order and "
                  "must be contiguous");
    errno = ERANGE;
    return -1;
  }
  exts->next = offset + length;

  /* Ignore zero-length extents. */
  if (length == 0)
    return 0;

  /* Ignore extents beyond the end of the range, or if list is full. */
  if (offset >= exts->end || exts->extents.len >= MAX_EXTENTS)
    return 0;

  /* Shorten extents that overlap the end of the range. */
  if (offset + length > exts->end) {
    overlap = offset + length - exts->end;
    length -= overlap;
  }

  if (exts->extents.len == 0) {
    /* If there are no existing extents, and the new extent is
     * entirely before start, ignore it.
     */
    if (offset + length <= exts->start)
      return 0;

    /* If there are no existing extents, and the new extent is after
     * start, then this is a bug in the plugin.
     */
    if (offset > exts->start) {
      nbdkit_error ("nbdkit_add_extent: "
                    "first extent must not be > start (%" PRIu64 ")",
                    exts->start);
      errno = ERANGE;
      return -1;
    }

    /* If there are no existing extents, and the new extent overlaps
     * start, truncate it so it starts at start.
     */
    overlap = exts->start - offset;
    length -= overlap;
    offset += overlap;
  }

  /* If we get here we are going to either add or extend. */
  if (exts->extents.len > 0 &&
      exts->extents.ptr[exts->extents.len-1].type == type) {
    /* Coalesce with the last extent. */
    exts->extents.ptr[exts->extents.len-1].length += length;
    return 0;
  }
  else {
    /* Add a new extent. */
    const struct nbdkit_extent e =
      { .offset = offset, .length = length, .type = type };
    return append_extent (exts, &e);
  }
}

/* Compute aligned extents on behalf of a filter. */
NBDKIT_DLL_PUBLIC int
nbdkit_extents_aligned (struct context *next_c,
                        uint32_t count, uint64_t offset,
                        uint32_t flags, uint32_t align,
                        struct nbdkit_extents *exts, int *err)
{
  struct nbdkit_next_ops *next = &next_c->next;
  size_t i;
  struct nbdkit_extent *e, *e2;

  assert (IS_ALIGNED (count | offset, align));

  /* Perform an initial query, then scan for the first unaligned extent. */
  if (next->extents (next_c, count, offset, flags, exts, err) == -1)
    return -1;
  for (i = 0; i < exts->extents.len; ++i) {
    e = &exts->extents.ptr[i];
    if (!IS_ALIGNED (e->length, align)) {
      /* If the unalignment is past align, just truncate and return early */
      if (e->offset + e->length > offset + align) {
        e->length = ROUND_DOWN (e->length, align);
        exts->extents.len = i + !!e->length;
        exts->next = e->offset + e->length;
        break;
      }

      /* Otherwise, coalesce until we have at least align bytes, which
       * may require further queries. The type bits are:
       *  NBDKIT_EXTENT_HOLE (1<<0)
       *  NBDKIT_EXTENT_ZERO (1<<1)
       * and the NBD protocol says any future bits will also have the
       * desired property that returning '0' is the safe default for
       * the generic case.  Thus, performing the bitwise-and
       * intersection of the types of underlying extents gives the
       * correct type for our merged extent.
       */
      assert (i == 0);
      while (e->length < align) {
        if (exts->extents.len > 1) {
          e->length += exts->extents.ptr[1].length;
          e->type &= exts->extents.ptr[1].type;
          extents_remove (&exts->extents, 1);
        }
        else {
          /* The plugin needs a fresh extents object each time, but
           * with care, we can merge it into the callers' exts.
           */
          extents tmp;
          CLEANUP_EXTENTS_FREE struct nbdkit_extents *extents2 = NULL;

          extents2 = nbdkit_extents_new (e->offset + e->length,
                                         offset + align);
          if (extents2 == NULL) {
            *err = errno;
            return -1;
          }
          if (next->extents (next_c, align - e->length,
                             offset + e->length,
                             flags & ~NBDKIT_FLAG_REQ_ONE,
                             extents2, err) == -1)
            return -1;
          e2 = &extents2->extents.ptr[0];
          assert (e2->offset == e->offset + e->length);
          e2->offset = e->offset;
          e2->length += e->length;
          e2->type &= e->type;
          e = e2;
          tmp = exts->extents;
          exts->extents = extents2->extents;
          extents2->extents = tmp;
        }
      }
      e->length = align;
      exts->extents.len = 1;
      exts->next = e->offset + e->length;
      break;
    }
  }
  /* Once we get here, all extents are aligned. */
  return 0;
}

/* This is a convenient wrapper around next_ops->extents which can be
 * used from filters where you want to get a complete set of extents
 * covering the region [offset..offset+count-1].
 */
NBDKIT_DLL_PUBLIC struct nbdkit_extents *
nbdkit_extents_full (struct context *next_c,
                     uint32_t count, uint64_t offset, uint32_t flags,
                     int *err)
{
  struct nbdkit_next_ops *next = &next_c->next;
  struct nbdkit_extents *ret;

  /* Clear REQ_ONE to ask the plugin for as much information as it is
   * willing to return (the plugin may still truncate if it is too
   * costly to provide everything).
   */
  flags &= ~NBDKIT_FLAG_REQ_ONE;

  ret = nbdkit_extents_new (offset, offset+count);
  if (ret == NULL) goto error0;

  while (count > 0) {
    const uint64_t old_offset = offset;
    size_t i;

    CLEANUP_EXTENTS_FREE struct nbdkit_extents *t
      = nbdkit_extents_new (offset, offset+count);
    if (t == NULL) goto error1;

    if (next->extents (next_c, count, offset, flags, t, err) == -1)
      goto error0;

    for (i = 0; i < nbdkit_extents_count (t); ++i) {
      const struct nbdkit_extent e = nbdkit_get_extent (t, i);
      if (nbdkit_add_extent (ret, e.offset, e.length, e.type) == -1)
        goto error1;

      assert (e.length <= count);
      offset += e.length;
      count -= e.length;
    }

    /* If the plugin is behaving we must make forward progress. */
    assert (offset > old_offset);
  }

  return ret;

 error1:
  *err = errno;
 error0:
  nbdkit_extents_free (ret);
  return NULL;
}
