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
#include <inttypes.h>

#include "array-size.h"
#include "random.h"

/* This works by comparing the result to some known test vectors.  It
 * should produce the same result on all architectures, platforms and
 * compilers.
 */

#define LEN 16

struct {
  uint64_t seed;
  uint64_t vector[LEN];
} tests[] = {
  { 0, {
UINT64_C (0x99ec5f36cb75f2b4),
UINT64_C (0xbf6e1f784956452a),
UINT64_C (0x1a5f849d4933e6e0),
UINT64_C (0x6aa594f1262d2d2c),
UINT64_C (0xbba5ad4a1f842e59),
UINT64_C (0xffef8375d9ebcaca),
UINT64_C (0x6c160deed2f54c98),
UINT64_C (0x8920ad648fc30a3f),
UINT64_C (0xdb032c0ba7539731),
UINT64_C (0xeb3a475a3e749a3d),
UINT64_C (0x1d42993fa43f2a54),
UINT64_C (0x11361bf526a14bb5),
UINT64_C (0x1b4f07a5ab3d8e9c),
UINT64_C (0xa7a3257f6986db7f),
UINT64_C (0x7efdaa95605dfc9c),
UINT64_C (0x4bde97c0a78eaab8),
    } },
  { 1, {
UINT64_C (0xb3f2af6d0fc710c5),
UINT64_C (0x853b559647364cea),
UINT64_C (0x92f89756082a4514),
UINT64_C (0x642e1c7bc266a3a7),
UINT64_C (0xb27a48e29a233673),
UINT64_C (0x24c123126ffda722),
UINT64_C (0x123004ef8df510e6),
UINT64_C (0x61954dcc47b1e89d),
UINT64_C (0xddfdb48ab9ed4a21),
UINT64_C (0x8d3cdb8c3aa5b1d0),
UINT64_C (0xeebd114bd87226d1),
UINT64_C (0xf50c3ff1e7d7e8a6),
UINT64_C (0xeeca3115e23bc8f1),
UINT64_C (0xab49ed3db4c66435),
UINT64_C (0x99953c6c57808dd7),
UINT64_C (0xe3fa941b05219325),
} },
  { 2, {
UINT64_C (0x1a28690da8a8d057),
UINT64_C (0xb9bb8042daedd58a),
UINT64_C (0x2f1829af001ef205),
UINT64_C (0xbf733e63d139683d),
UINT64_C (0xafa78247c6a82034),
UINT64_C (0x3c69a1b6d15cf0d0),
UINT64_C (0xa5a9fdd18948c400),
UINT64_C (0x3813d2654a981e91),
UINT64_C (0x9be35597c9c97bfa),
UINT64_C (0xbfc5e80fd0b75f32),
UINT64_C (0xbee02daaac716557),
UINT64_C (0x5afed6f12b594dbe),
UINT64_C (0xae346b9196e12cc7),
UINT64_C (0xf5f45afc1af068ed),
UINT64_C (0xff75eccacfb37519),
UINT64_C (0x1adca5a0b2e766c5),
} },
  { 3, {
UINT64_C (0xb0cdabdae5668cc0),
UINT64_C (0xa3fd1dea5e1864ee),
UINT64_C (0x37e00afb3229fd51),
UINT64_C (0x88b1b58b236f3bea),
UINT64_C (0x6cb24c8fb224980a),
UINT64_C (0x6646287ee2a98083),
UINT64_C (0x35cd8bb5e1fa7256),
UINT64_C (0xb72fe6e16b6fb4e6),
UINT64_C (0xf1397a9f1db4f5d9),
UINT64_C (0x31f25047faa8e5d4),
UINT64_C (0xec616a6e46e96dec),
UINT64_C (0xae0c5e0f7b5d1449),
UINT64_C (0xa517e799c5c6e32f),
UINT64_C (0xc1276908f843b688),
UINT64_C (0xaf7e924d738d87ec),
UINT64_C (0x1c3f3ba863d5c7d1),
} },
  { 4, {
UINT64_C (0x437057a4eb7c3a13),
UINT64_C (0xe95a0d7fd8c1832c),
UINT64_C (0x71807ff81a0c627e),
UINT64_C (0xfa40f34634632cd2),
UINT64_C (0x39cf61fc694b95b7),
UINT64_C (0x9ca3d6e037621a02),
UINT64_C (0x7be965236729c7d3),
UINT64_C (0xb95fba07afa980ac),
UINT64_C (0x91424978ab94232),
UINT64_C (0x565eb8170fdae341),
UINT64_C (0x0744508beb95a6bb),
UINT64_C (0xf2426b33aa0a601d),
UINT64_C (0x7ddc1fcd0bfec893),
UINT64_C (0x9e09fedd4af1ff3d),
UINT64_C (0xbe77c1bed02132e7),
UINT64_C (0x61e4f6e3e88d34d4),
} },
  { UINT64_MAX, {
UINT64_C (0x8f5520d52a7ead08),
UINT64_C (0xc476a018caa1802d),
UINT64_C (0x81de31c0d260469e),
UINT64_C (0xbf658d7e065f3c2f),
UINT64_C (0x913593fda1bca32a),
UINT64_C (0xbb535e93941ba525),
UINT64_C (0x5ecda415c3c6dfde),
UINT64_C (0xc487398fc9de9ae2),
UINT64_C (0xa06746dbb57c4d62),
UINT64_C (0x9d414196fdf05c8a),
UINT64_C (0x41cf1af9a178c669),
UINT64_C (0x0b3b3a95e78839f9),
UINT64_C (0x7aaab30444aefc7e),
UINT64_C (0x7b251ec961f341b1),
UINT64_C (0x30ed32acf367205f),
UINT64_C (0xc6ca62fc772728b0),
} },
};

int
main (void)
{
  size_t i, j;
  uint64_t r;
  unsigned errors = 0;

  for (i = 0; i < ARRAY_SIZE (tests); ++i) {
    struct random_state state;

    printf ("seed: %" PRIu64 "\n", tests[i].seed);
    xsrandom (tests[i].seed, &state);
    for (j = 0; j < LEN; ++j) {
      r = xrandom (&state);
      if (tests[i].vector[j] != r) {
        printf ("\texpected: 0x%" PRIx64 "\tactual: 0x%" PRIx64 "\n",
                tests[i].vector[j], r);
        errors++;
      }
    }
  }

  if (errors > 0) {
    fprintf (stderr, "random vector does not match expected\n");
    exit (EXIT_FAILURE);
  }

  printf ("test successful\n");
  exit (EXIT_SUCCESS);
}
