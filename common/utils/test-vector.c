/* nbdkit
 * Copyright (C) 2020 Red Hat Inc.
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
#undef NDEBUG /* Keep test strong even for nbdkit built without assertions */
#include <assert.h>

#include "bench.h"
#include "vector.h"

#define APPENDS 1000000

DEFINE_VECTOR_TYPE(int64_vector, int64_t);
DEFINE_VECTOR_TYPE(uint32_vector, uint32_t);
DEFINE_VECTOR_TYPE(string_vector, char *);

static int
compare (const int64_t *a, const int64_t *b)
{
  return (*a > *b) - (*a < *b);
}

static void
test_int64_vector (void)
{
  int64_vector v = empty_vector;
  size_t i;
  int r;
  int64_t tmp, *p;

  for (i = 0; i < 10; ++i) {
    r = int64_vector_insert (&v, i, 0);
    assert (r == 0);
  }

  for (i = 0; i < 10; ++i)
    assert (v.ptr[i] == 9 - i);
  int64_vector_sort (&v, compare);
  for (i = 0; i < 10; ++i)
    assert (v.ptr[i] == i);

  int64_vector_remove (&v, 1);
  assert (v.size == 9);
  assert (v.ptr[1] == 2);

  tmp = 10;
  p = int64_vector_search (&v, &tmp, (void*) compare);
  assert (p == NULL);
  tmp = 8;
  p = int64_vector_search (&v, &tmp, (void*) compare);
  assert (p == &v.ptr[7]);

  free (v.ptr);
}

static void
test_string_vector (void)
{
  string_vector v = empty_vector;
  size_t i;
  int r;

  for (i = 0; i < 10; ++i) {
    char *s;

    r = asprintf (&s, "number %zu", i);
    assert (r >= 0);
    r = string_vector_append (&v, s);
    assert (r == 0);
  }
  /* NULL-terminate the strings. */
  r = string_vector_append (&v, NULL);
  assert (r == 0);

  /* Now print them. */
  for (i = 0; v.ptr[i] != NULL; ++i)
    printf ("%s\n", v.ptr[i]);
  assert (i == 10);

  /* And free them.  We can use the generated iter function here
   * even though it calls free on the final NULL pointer.
   */
  string_vector_iter (&v, (void*)free);
  free (v.ptr);
}

static void
bench_reserve (void)
{
  uint32_vector v = empty_vector;
  struct bench b;

  bench_start(&b);

  uint32_vector_reserve(&v, APPENDS);

  for (uint32_t i = 0; i < APPENDS; i++) {
    uint32_vector_append (&v, i);
  }

  bench_stop(&b);

  assert (v.ptr[APPENDS - 1] == APPENDS - 1);
  free (v.ptr);

  printf ("bench_reserve: %d appends in %.6f s\n", APPENDS, bench_sec (&b));
}

static void
bench_append (void)
{
  uint32_vector v = empty_vector;
  struct bench b;

  bench_start(&b);

  for (uint32_t i = 0; i < APPENDS; i++) {
    uint32_vector_append (&v, i);
  }

  bench_stop(&b);

  assert (v.ptr[APPENDS - 1] == APPENDS - 1);
  free (v.ptr);

  printf ("bench_append: %d appends in %.6f s\n", APPENDS, bench_sec (&b));
}

int
main (int argc, char *argv[])
{
  if (getenv("NBDKIT_BENCH")) {
    bench_reserve ();
    bench_append ();
  } else {
    test_int64_vector ();
    test_string_vector ();
  }
  return 0;
}
