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
#include <string.h>
#undef NDEBUG /* Keep test strong even for nbdkit built without assertions */
#include <assert.h>

#include "byte-swapping.h"

/* Little-endian test strings. */
static uint8_t le16[] = { 0x34, 0x12 };
static uint8_t le32[] = { 0x78, 0x56, 0x34, 0x12 };
static uint8_t le64[] = { 0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12 };

/* Big-endian test strings. */
static uint8_t be16[] = { 0x12, 0x34 };
static uint8_t be32[] = { 0x12, 0x34, 0x56, 0x78 };
static uint8_t be64[] = { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0 };

int
main (void)
{
  uint16_t i16;
  uint32_t i32;
  uint64_t i64;

  memcpy (&i16, le16, 2);
  assert (le16toh (i16) == 0x1234);
  memcpy (&i32, le32, 4);
  assert (le32toh (i32) == 0x12345678);
  memcpy (&i64, le64, 8);
  assert (le64toh (i64) == 0x123456789abcdef0);

  memcpy (&i16, be16, 2);
  assert (be16toh (i16) == 0x1234);
  memcpy (&i32, be32, 4);
  assert (be32toh (i32) == 0x12345678);
  memcpy (&i64, be64, 8);
  assert (be64toh (i64) == 0x123456789abcdef0);

  i16 = htole16 (0x1234);
  assert (memcmp (&i16, le16, 2) == 0);
  i32 = htole32 (0x12345678);
  assert (memcmp (&i32, le32, 4) == 0);
  i64 = htole64 (0x123456789abcdef0);
  assert (memcmp (&i64, le64, 8) == 0);

  i16 = htobe16 (0x1234);
  assert (memcmp (&i16, be16, 2) == 0);
  i32 = htobe32 (0x12345678);
  assert (memcmp (&i32, be32, 4) == 0);
  i64 = htobe64 (0x123456789abcdef0);
  assert (memcmp (&i64, be64, 8) == 0);

  memcpy (&i16, le16, 2);
  i16 = bswap_16 (i16);
  assert (memcmp (&i16, be16, 2) == 0);
  i16 = bswap_16 (i16);
  assert (memcmp (&i16, le16, 2) == 0);

  memcpy (&i32, le32, 4);
  i32 = bswap_32 (i32);
  assert (memcmp (&i32, be32, 4) == 0);
  i32 = bswap_32 (i32);
  assert (memcmp (&i32, le32, 4) == 0);

  memcpy (&i64, le64, 8);
  i64 = bswap_64 (i64);
  assert (memcmp (&i64, be64, 8) == 0);
  i64 = bswap_64 (i64);
  assert (memcmp (&i64, le64, 8) == 0);

  exit (EXIT_SUCCESS);
}
