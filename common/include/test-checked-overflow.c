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

#include <stddef.h>
#include <stdint.h>
#undef NDEBUG /* Keep test strong even for nbdkit built without assertions */
#include <assert.h>

#include "checked-overflow.h"

#define TEST_ADD(a, b, result, expected_overflow, expected_result)      \
  do {                                                                  \
    bool actual_overflow;                                               \
                                                                        \
    actual_overflow = ADD_OVERFLOW_FALLBACK ((a), (b), (result));       \
    assert (actual_overflow == (expected_overflow));                    \
    assert (*(result) == (expected_result));                            \
    actual_overflow = ADD_OVERFLOW_FALLBACK ((b), (a), (result));       \
    assert (actual_overflow == (expected_overflow));                    \
    assert (*(result) == (expected_result));                            \
  } while (0)

#define TEST_MUL(a, b, result, expected_overflow, expected_result)      \
  do {                                                                  \
    bool actual_overflow;                                               \
                                                                        \
    actual_overflow = MUL_OVERFLOW_FALLBACK ((a), (b), (result));       \
    assert (actual_overflow == (expected_overflow));                    \
    assert (*(result) == (expected_result));                            \
    actual_overflow = MUL_OVERFLOW_FALLBACK ((b), (a), (result));       \
    assert (actual_overflow == (expected_overflow));                    \
    assert (*(result) == (expected_result));                            \
  } while (0)

/* Define these const-qualified objects because the UINTN_MAX object-like
 * macros in <stdint.h> have "post integer promotion" types. Therefore,
 * UINT16_MAX and UINT8_MAX most commonly have type "int". And that trips the
 * signedness check in ADD_OVERFLOW_FALLBACK().
 */
static const uintmax_t umax_max = UINTMAX_MAX;
static const uint64_t  u64_max  = UINT64_MAX;
static const uint32_t  u32_max  = UINT32_MAX;
static const uint16_t  u16_max  = UINT16_MAX;
static const uint8_t   u8_max   = UINT8_MAX;
static const size_t    size_max = SIZE_MAX;

int
main (void)
{
  union {
    uintmax_t umax;
    uint64_t  u64;
    uint32_t  u32;
    uint16_t  u16;
    uint8_t   u8;
    size_t    sz;
  } result;
  bool overflow;

  /* "max + 0" and "0 + max" evaluate to "max", without overflow. */
  TEST_ADD (umax_max, 0u, &result.umax, false, umax_max);
  TEST_ADD (u64_max,  0u, &result.u64,  false, u64_max);
  TEST_ADD (u32_max,  0u, &result.u32,  false, u32_max);
  TEST_ADD (u16_max,  0u, &result.u16,  false, u16_max);
  TEST_ADD (u8_max,   0u, &result.u8,   false, u8_max);
  TEST_ADD (size_max, 0u, &result.sz,   false, size_max);

  /* "max + 1" and "1 + max" overflow to zero. */
  TEST_ADD (umax_max, 1u, &result.umax, true, 0);
  TEST_ADD (u64_max,  1u, &result.u64,  true, 0);
  TEST_ADD (u32_max,  1u, &result.u32,  true, 0);
  TEST_ADD (u16_max,  1u, &result.u16,  true, 0);
  TEST_ADD (u8_max,   1u, &result.u8,   true, 0);
  TEST_ADD (size_max, 1u, &result.sz,   true, 0);

  /* Adding umax_max (i.e., all-bits-one) amounts (with overflow) to
   * subtracting one.
   */
  TEST_ADD (umax_max, umax_max, &result.umax, true, umax_max - 1);
  TEST_ADD (u64_max,  umax_max, &result.u64,  true, u64_max  - 1);
  TEST_ADD (u32_max,  umax_max, &result.u32,  true, u32_max  - 1);
  TEST_ADD (u16_max,  umax_max, &result.u16,  true, u16_max  - 1);
  TEST_ADD (u8_max,   umax_max, &result.u8,   true, u8_max   - 1);
  TEST_ADD (size_max, umax_max, &result.sz,   true, size_max - 1);

  /* "max * 0" and "0 * max" evaluate to 0, without overflow. */
  TEST_MUL (umax_max, 0u, &result.umax, false, 0);
  TEST_MUL (u64_max,  0u, &result.u64,  false, 0);
  TEST_MUL (u32_max,  0u, &result.u32,  false, 0);
  TEST_MUL (u16_max,  0u, &result.u16,  false, 0);
  TEST_MUL (u8_max,   0u, &result.u8,   false, 0);
  TEST_MUL (size_max, 0u, &result.sz,   false, 0);

  /* "max * 1" and "1 * max" evaluate to "max", without overflow. */
  TEST_MUL (umax_max, 1u, &result.umax, false, umax_max);
  TEST_MUL (u64_max,  1u, &result.u64,  false, u64_max);
  TEST_MUL (u32_max,  1u, &result.u32,  false, u32_max);
  TEST_MUL (u16_max,  1u, &result.u16,  false, u16_max);
  TEST_MUL (u8_max,   1u, &result.u8,   false, u8_max);
  TEST_MUL (size_max, 1u, &result.sz,   false, size_max);

  /* "max * 2" and "2 * max" evaluate (with overflow) to "max - 1". */
  TEST_MUL (umax_max, 2u, &result.umax, true, umax_max - 1);
  TEST_MUL (u64_max,  2u, &result.u64,  true, u64_max  - 1);
  TEST_MUL (u32_max,  2u, &result.u32,  true, u32_max  - 1);
  TEST_MUL (u16_max,  2u, &result.u16,  true, u16_max  - 1);
  TEST_MUL (u8_max,   2u, &result.u8,   true, u8_max   - 1);
  TEST_MUL (size_max, 2u, &result.sz,   true, size_max - 1);

  /* factor                  255 -> 3 5 17
   * factor                65535 -> 3 5 17 257
   * factor           4294967295 -> 3 5 17 257     65537
   * factor 18446744073709551615 -> 3 5 17 257 641 65537 6700417
   *
   * Note: every time we double the width, we multiply the previous maximum
   * 0xF...F with 0x10...01:
   *
   *        0xF (= 3 * 5) *        0x11 (=            17) =               0xFF
   *       0xFF           *       0x101 (=           257) =             0xFFFF
   *     0xFFFF           *     0x10001 (=         65537) =         0xFFFFFFFF
   * 0xFFFFFFFF           * 0x100000001 (= 641 * 6700417) = 0xFFFFFFFFFFFFFFFF
   *
   * Perform the above multiplications, advacing with prime factors.
   */
  overflow = MUL_OVERFLOW_FALLBACK (3u, 5u, &result.u8);
  assert (!overflow);
  assert (result.u8 == 0xF);

  overflow = MUL_OVERFLOW_FALLBACK (result.u8, 17u, &result.u8);
  assert (!overflow);
  assert (result.u8 == UINT8_MAX);

  overflow = MUL_OVERFLOW_FALLBACK (result.u8, 257u, &result.u16);
  assert (!overflow);
  assert (result.u16 == UINT16_MAX);

  overflow = MUL_OVERFLOW_FALLBACK (result.u16, 65537ul, &result.u32);
  assert (!overflow);
  assert (result.u32 == UINT32_MAX);

  overflow = MUL_OVERFLOW_FALLBACK (result.u32, 641u, &result.u64);
  assert (!overflow);
  overflow = MUL_OVERFLOW_FALLBACK (result.u64, 6700417ul, &result.u64);
  assert (!overflow);
  assert (result.u64 == UINT64_MAX);

  return 0;
}
