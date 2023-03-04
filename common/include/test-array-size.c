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
#undef NDEBUG /* Keep test strong even for nbdkit built without assertions */
#include <assert.h>

#include "array-size.h"
#include "static-assert.h"

struct st { const char *s; int i; };

static const char *s0[] __attribute__ ((__unused__)) = { };
static const char *s1[] __attribute__ ((__unused__)) = { "a" };
static const char *s3[] __attribute__ ((__unused__)) = { "a", "b", "c" };
static const char *s4[4] __attribute__ ((__unused__)) = { "a", "b", "c", "d" };
static int i0[] __attribute__ ((__unused__)) = { };
static int i1[] __attribute__ ((__unused__)) = { 1 };
static int i3[] __attribute__ ((__unused__)) = { 1, 2, 3 };
static int i4[4] __attribute__ ((__unused__)) = { 1, 2, 3, 4 };
static struct st st0[] __attribute__ ((__unused__)) = { };
static struct st st1[] __attribute__ ((__unused__)) = { { "a", 1 } };
static struct st st3[] __attribute__ ((__unused__)) =
  { { "a", 1 }, { "b", 2 }, { "c", 3 } };
static struct st st4[4] __attribute__ ((__unused__)) =
  { { "a", 1 }, { "b", 2 }, { "c", 3 }, { "d", 4 } };
static struct st st4_0[4] __attribute__ ((__unused__));

int
main (void)
{
  STATIC_ASSERT (ARRAY_SIZE (s0) == 0, _array_size_macro_works);
  STATIC_ASSERT (ARRAY_SIZE (s1) == 1, _array_size_macro_works);
  STATIC_ASSERT (ARRAY_SIZE (s3) == 3, _array_size_macro_works);
  STATIC_ASSERT (ARRAY_SIZE (s4) == 4, _array_size_macro_works);
  STATIC_ASSERT (ARRAY_SIZE (i0) == 0, _array_size_macro_works);
  STATIC_ASSERT (ARRAY_SIZE (i1) == 1, _array_size_macro_works);
  STATIC_ASSERT (ARRAY_SIZE (i3) == 3, _array_size_macro_works);
  STATIC_ASSERT (ARRAY_SIZE (i4) == 4, _array_size_macro_works);
  STATIC_ASSERT (ARRAY_SIZE (st0) == 0, _array_size_macro_works);
  STATIC_ASSERT (ARRAY_SIZE (st1) == 1, _array_size_macro_works);
  STATIC_ASSERT (ARRAY_SIZE (st3) == 3, _array_size_macro_works);
  STATIC_ASSERT (ARRAY_SIZE (st4) == 4, _array_size_macro_works);
  STATIC_ASSERT (ARRAY_SIZE (st4_0) == 4, _array_size_macro_works);

  /* You can uncomment this to test the negative case.  Unfortunately
   * it's difficult to automate this test.
   */
#if 0
  int *p = i4;
  STATIC_ASSERT (ARRAY_SIZE (p) == 4, _array_size_macro_is_applied_to_array);
#endif

  exit (EXIT_SUCCESS);
}
