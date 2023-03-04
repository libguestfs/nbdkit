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

#ifndef NBDKIT_RANDOM_H
#define NBDKIT_RANDOM_H

#include <stdint.h>

/* Generate pseudo-random numbers, quickly, with explicit state.
 *
 * This is based on the xoshiro/xoroshiro generators by David Blackman
 * and Sebastiano Vigna (http://xoshiro.di.unimi.it/).  Specifically
 * the main PRNG is ‘xoshiro256** 1.0’, and the seed generator is
 * ‘splitmix64’.
 *
 * This does _NOT_ generate cryptographically secure random numbers
 * (CSPRNG) and so should not be used when cryptography or security is
 * required - use gcrypt if you need those.
 */

/* You can seed ‘struct random_state’ by setting the s[] elements
 * directly - but you must NOT set them all to zero.  Alternately if
 * you have a 64 bit seed, you can call xsrandom to initialize the
 * state.
 */
struct random_state {
  uint64_t s[4];
};

static inline uint64_t
snext (uint64_t *seed)
{
  uint64_t z = (*seed += 0x9e3779b97f4a7c15);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
  z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
  return z ^ (z >> 31);
}

/* Seed the random state from a 64 bit seed. */
static inline void  __attribute__ ((__nonnull__ (2)))
xsrandom (uint64_t seed, struct random_state *state)
{
  state->s[0] = snext (&seed);
  state->s[1] = snext (&seed);
  state->s[2] = snext (&seed);
  state->s[3] = snext (&seed);
}

static inline uint64_t
rotl (const uint64_t x, int k)
{
  /* RWMJ: I checked and GCC 10 emits either ‘rol’ or ‘ror’ correctly
   * for this code.
   */
  return (x << k) | (x >> (64 - k));
}

/* Returns 64 random bits.  Updates the state. */
static inline uint64_t __attribute__ ((__nonnull__ (1)))
xrandom (struct random_state *state)
{
  const uint64_t result_starstar = rotl (state->s[1] * 5, 7) * 9;
  const uint64_t t = state->s[1] << 17;

  state->s[2] ^= state->s[0];
  state->s[3] ^= state->s[1];
  state->s[1] ^= state->s[2];
  state->s[0] ^= state->s[3];

  state->s[2] ^= t;

  state->s[3] = rotl (state->s[3], 45);

  return result_starstar;
}

#endif /* NBDKIT_RANDOM_H */
