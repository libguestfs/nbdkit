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

#ifndef NBDKIT_ISZERO_H
#define NBDKIT_ISZERO_H

#include <string.h>
#include <stdbool.h>

/* Return true iff the buffer is all zero bytes.
 *
 * The clever approach here was suggested by Eric Blake.  See:
 * https://www.redhat.com/archives/libguestfs/2017-April/msg00171.html
 * https://rusty.ozlabs.org/?p=560
 *
 * See also:
 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=69908
 */
static inline bool __attribute__ ((__nonnull__ (1)))
is_zero (const char *buffer, size_t size)
{
  size_t i;
  const size_t limit = size < 16 ? size : 16;

  for (i = 0; i < limit; ++i)
    if (buffer[i])
      return false;
  if (size != limit)
    return ! memcmp (buffer, buffer + 16, size - 16);

  return true;
}

#endif /* NBDKIT_ISZERO_H */
