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
#include <limits.h>
#undef NDEBUG /* Keep test strong even for nbdkit built without assertions */
#include <assert.h>
#include <float.h>

#include "minmax.h"

#define SIGNED_TEST(var, min, max)                \
  MIN_SIGNED_TEST (var, min, max)                 \
  MAX_SIGNED_TEST (var, min, max)

#define MIN_SIGNED_TEST(var, min, max)          \
  var = 0;                                      \
  assert (MIN (0, var) == 0);                   \
  var = 1;                                      \
  assert (MIN (0, var) == 0);                   \
  var = -1;                                     \
  assert (MIN (0, var) == -1);                  \
  var = 1;                                      \
  assert (MIN (var, 0) == 0);                   \
  var = 1;                                      \
  assert (MIN (var, 1) == 1);                   \
  var = -1;                                     \
  assert (MIN (var, 0) == -1);                  \
  var = min;                                    \
  assert (MIN (var, min) == min);               \
  var = max;                                    \
  assert (MIN (var, max) == max);               \
  var = min;                                    \
  assert (MIN (var, max) == min);               \
  assert (MIN (0, min) == min);

#define MAX_SIGNED_TEST(var, min, max)          \
  var = 0;                                      \
  assert (MAX (0, var) == 0);                   \
  var = 1;                                      \
  assert (MAX (0, var) == 1);                   \
  var = -1;                                     \
  assert (MAX (0, var) == 0);                   \
  var = 1;                                      \
  assert (MAX (var, 0) == 1);                   \
  var = 1;                                      \
  assert (MAX (var, 1) == 1);                   \
  var = -1;                                     \
  assert (MAX (var, 0) == 0);                   \
  var = min;                                    \
  assert (MAX (var, min) == min);               \
  var = max;                                    \
  assert (MAX (var, max) == max);               \
  var = min;                                    \
  assert (MAX (var, max) == max);               \
  assert (MAX (0, min) == 0);

#define UNSIGNED_TEST(var, max)                \
  MIN_UNSIGNED_TEST (var, max)                 \
  MAX_UNSIGNED_TEST (var, max)

#define MIN_UNSIGNED_TEST(var, max)             \
  var = 0;                                      \
  assert (MIN (0, var) == 0);                   \
  var = 1;                                      \
  assert (MIN (0, var) == 0);                   \
  var = 1;                                      \
  assert (MIN (var, 0) == 0);                   \
  var = 1;                                      \
  assert (MIN (var, 1) == 1);                   \
  var = max;                                    \
  assert (MIN (var, max) == max);

#define MAX_UNSIGNED_TEST(var, max)             \
  var = 0;                                      \
  assert (MAX (0, var) == 0);                   \
  var = 1;                                      \
  assert (MAX (0, var) == 1);                   \
  var = 1;                                      \
  assert (MAX (var, 0) == 1);                   \
  var = 1;                                      \
  assert (MAX (var, 1) == 1);                   \
  var = max;                                    \
  assert (MAX (var, max) == max);

int
main (void)
{
  signed char sc;
  int i;
  int8_t i8;
  int16_t i16;
  int32_t i32;
  int64_t i64;
  unsigned char uc;
  unsigned u;
  uint8_t u8;
  uint16_t u16;
  uint32_t u32;
  uint64_t u64;
  float f;
  double d;

  SIGNED_TEST (sc, SCHAR_MIN, SCHAR_MAX);
  SIGNED_TEST (i, INT_MIN, INT_MAX);
  SIGNED_TEST (i8, INT8_MIN, INT8_MAX);
  SIGNED_TEST (i16, INT16_MIN, INT16_MAX);
  SIGNED_TEST (i32, INT32_MIN, INT32_MAX);
  SIGNED_TEST (i64, INT64_MIN, INT64_MAX);

  UNSIGNED_TEST (uc, UCHAR_MAX);
  UNSIGNED_TEST (u, UINT_MAX);
  UNSIGNED_TEST (u8, UINT8_MAX);
  UNSIGNED_TEST (u16, UINT16_MAX);
  UNSIGNED_TEST (u32, UINT32_MAX);
  UNSIGNED_TEST (u64, UINT64_MAX);

  /* Note that FLT_MIN and DBL_MIN are the closest positive normalized
   * numbers to 0.0, not the min.
   */
  SIGNED_TEST (f, -FLT_MAX, FLT_MAX);
  SIGNED_TEST (d, -DBL_MAX, DBL_MAX);

  /* Test that MIN and MAX can be nested.  This is really a compile
   * test, but we do check the answer.
   */
  assert (MIN (MIN (1, 2), 3) == 1);
  assert (MAX (MIN (1, 2), 3) == 3);
  assert (MIN (MAX (1, 2), 3) == 2);
  assert (MAX (MAX (1, 4), 3) == 4);
  assert (MIN (3, MIN (1, 2)) == 1);
  assert (MAX (3, MIN (1, 2)) == 3);
  assert (MIN (3, MAX (1, 2)) == 2);
  assert (MAX (3, MAX (1, 4)) == 4);
  assert (MIN (MIN (1, MIN (2, 3)), 4) == 1);
  assert (MAX (MAX (1, MAX (2, 3)), 4) == 4);

  exit (EXIT_SUCCESS);
}
