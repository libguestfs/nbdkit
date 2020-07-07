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
#include <assert.h>

#include "vector.h"

DEFINE_VECTOR_TYPE(int64_vector, int64_t);
DEFINE_VECTOR_TYPE(string_vector, char *);

static void
test_int64_vector (void)
{
  int64_vector v = empty_vector;
  size_t i;
  int r;

  for (i = 0; i < 10; ++i) {
    r = int64_vector_append (&v, i);
    assert (r == 0);
  }
  for (i = 0; i < 10; ++i)
    assert (v.ptr[i] == i);
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

int
main (int argc, char *argv[])
{
  test_int64_vector ();
  test_string_vector ();
}
