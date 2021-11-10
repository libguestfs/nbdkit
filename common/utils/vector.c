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
#include <errno.h>

#include "checked-overflow.h"
#include "vector.h"

int
generic_vector_reserve (struct generic_vector *v, size_t n, size_t itemsize)
{
  void *newptr;
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
  if (ADD_OVERFLOW (v->cap, 1, &t) ||
      ADD_OVERFLOW (v->cap, t/2, &newcap) ||
      MUL_OVERFLOW (newcap, itemsize, &newbytes) ||
      newbytes < reqbytes) {
    /* If that either overflows or is less than the minimum requested,
     * fall back to the requested capacity.
     */
    newcap = reqcap;
    newbytes = reqbytes;
  }

  newptr = realloc (v->ptr, newbytes);
  if (newptr == NULL)
    return -1;

  v->ptr = newptr;
  v->cap = newcap;
  return 0;
}
