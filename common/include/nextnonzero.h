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

#ifndef NBDKIT_NEXTNONZERO_H
#define NBDKIT_NEXTNONZERO_H

/* Given a byte buffer, return a pointer to the first non-zero byte,
 * or return NULL if we reach the end of the buffer.
 *
 * XXX: Even with -O3 -mavx2, gcc 8.2.1 does a terrible job with this
 * loop, compiling it completely naively.  QEMU has an AVX2
 * implementation of a similar loop.
 *
 * See also:
 * https://sourceware.org/bugzilla/show_bug.cgi?id=19920
 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=69908
 */
static inline const char * __attribute__ ((__nonnull__ (1)))
next_non_zero (const char *buffer, size_t size)
{
  size_t i;

  for (i = 0; i < size; ++i)
    if (buffer[i] != 0)
      return &buffer[i];
  return NULL;
}

#endif /* NBDKIT_NEXTNONZERO_H */
