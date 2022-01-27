/* nbdkit
 * Copyright (C) 2018-2021 Red Hat Inc.
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
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include "checked-overflow.h"
#include "vector.h"

static int
calculate_capacity (struct generic_vector *v, size_t n, size_t itemsize,
                    size_t *newcap_r, size_t *newbytes_r)
{
  size_t reqcap, reqbytes, newcap, newbytes, t;

  /* New capacity requested.  We must allocate this minimum (or fail).
   *   reqcap = v->cap + n
   *   reqbytes = reqcap * itemsize
   */
  if (ADD_OVERFLOW (v->cap, n, &reqcap) ||
      MUL_OVERFLOW (reqcap, itemsize, &reqbytes)) {
    errno = ENOMEM;
    return -1;
  }

  /* However for the sake of optimization, scale buffer by 3/2 so that
   * repeated reservations don't call realloc often.
   *   newcap = v->cap + (v->cap + 1) / 2
   *   newbytes = newcap * itemsize
   */
  if (ADD_OVERFLOW (v->cap, 1u, &t) ||
      ADD_OVERFLOW (v->cap, t/2, &newcap) ||
      MUL_OVERFLOW (newcap, itemsize, &newbytes) ||
      newbytes < reqbytes) {
    /* If that either overflows or is less than the minimum requested,
     * fall back to the requested capacity.
     */
    newcap = reqcap;
    newbytes = reqbytes;
  }

  *newcap_r = newcap;
  *newbytes_r = newbytes;
  return 0;
}

int
generic_vector_reserve (struct generic_vector *v, size_t n, size_t itemsize)
{
  void *newptr;
  size_t newcap, newbytes;

  if (calculate_capacity (v, n, itemsize, &newcap, &newbytes) == -1)
    return -1;

  newptr = realloc (v->ptr, newbytes);
  if (newptr == NULL)
    return -1;

  v->ptr = newptr;
  v->cap = newcap;
  return 0;
}

/* Always allocates a buffer which is page aligned (both base and size). */
int
generic_vector_reserve_page_aligned (struct generic_vector *v,
                                     size_t n, size_t itemsize)
{
#ifdef HAVE_POSIX_MEMALIGN
  int r;
#endif
  void *newptr;
  size_t newcap, newbytes;
  long pagesize;
  size_t extra, extra_items;

  pagesize = sysconf (_SC_PAGE_SIZE);
  assert (pagesize > 1);

  assert (pagesize % itemsize == 0);

  if (calculate_capacity (v, n, itemsize, &newcap, &newbytes) == -1)
    return -1;

  /* If the new size (in bytes) is not a full page then we have to
   * round up to the next page.
   */
  extra = newbytes & (pagesize - 1);
  if (extra > 0) {
    extra_items = (pagesize - extra) / itemsize;

    if (ADD_OVERFLOW (newcap, extra_items, &newcap) ||
        ADD_OVERFLOW (newbytes, extra_items * itemsize, &newbytes)) {
      errno = ENOMEM;
      return -1;
    }
  }

#ifdef HAVE_POSIX_MEMALIGN
  if ((r = posix_memalign (&newptr, pagesize, newbytes)) != 0) {
    errno = r;
    return -1;
  }
#elif HAVE_VALLOC
  newptr = valloc (newbytes);
  if (newptr == NULL)
    return -1;
#else
#error "this platform does not have posix_memalign or valloc"
#endif

  /* How much to copy here?
   *
   * vector_reserve always makes the buffer larger so we don't have to
   * deal with the case of a shrinking buffer.
   *
   * Like the underlying realloc we do not have to zero the newly
   * reserved elements.
   *
   * However (like realloc) we have to copy the existing elements even
   * those that are not allocated and only reserved (between 'len' and
   * 'cap').
   *
   * The spec of vector is not clear on the last two points, but
   * existing code depends on this undocumented behaviour.
   */
  memcpy (newptr, v->ptr, v->cap * itemsize);
  free (v->ptr);
  v->ptr = newptr;
  v->cap = newcap;
  return 0;
}
