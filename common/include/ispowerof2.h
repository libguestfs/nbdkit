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

#ifndef NBDKIT_ISPOWEROF2_H
#define NBDKIT_ISPOWEROF2_H

#include <stdbool.h>

/* Returns true if v is a power of 2.
 *
 * Uses the algorithm described at
 * http://graphics.stanford.edu/~seander/bithacks.html#DetermineIfPowerOf2
 */
static inline bool
is_power_of_2 (unsigned long v)
{
  return v && ((v & (v - 1)) == 0);
}

/* Calculate log2(v) which is the size of the equivalent bit shift
 * for a power of 2.  For example log_2_bits (512) == 9.
 *
 * Note this is undefined for v == 0.
 *
 * __builtin_clzl is available in GCC and clang.
 */
static inline int
log_2_bits (unsigned long v)
{
  return SIZEOF_LONG*8 - __builtin_clzl (v) - 1;
}

#endif /* NBDKIT_ISPOWEROF2_H */
