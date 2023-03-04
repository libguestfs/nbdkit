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
#include <sys/time.h>

#include "tvdiff.h"

/* This is mainly a test of the compiler and platform rather than our
 * implementation.
 */

#define TEST_TVDIFF(tv1, tv2, expected)                                 \
  do {                                                                  \
    int64_t actual = tvdiff_usec (&(tv1), &(tv2));                      \
                                                                        \
    if (actual != (expected)) {                                         \
      fprintf (stderr,                                                  \
               "%s: unexpected result %" PRIi64 ", expecting %" PRIi64 "\n", \
               argv[0], actual, (int64_t) (expected));                  \
      errors++;                                                         \
    }                                                                   \
  } while (0)

#define TEST_SUBTRACT(tv1, tv2, exp_sec, exp_usec)                      \
  do {                                                                  \
    struct timeval z;                                                   \
                                                                        \
    subtract_timeval (&tv1, &tv2, &z);                                  \
    if (z.tv_sec != (exp_sec) || z.tv_usec != (exp_usec)) {             \
      fprintf (stderr,                                                  \
               "%s: unexpected (%ld, %d), expecting (%ld, %d)\n",       \
               argv[0],                                                 \
               (long) z.tv_sec, (int) z.tv_usec,                        \
               (long) (exp_sec), (int) (exp_usec));                     \
      errors++;                                                         \
    }                                                                   \
  } while (0)

int
main (int argc, char *argv[])
{
  struct timeval tv1, tv2;
  unsigned errors = 0;

  tv1.tv_sec = 1000;
  tv1.tv_usec = 1;
  TEST_TVDIFF (tv1, tv1, 0);
  TEST_SUBTRACT (tv1, tv1, 0, 0);

  tv2.tv_sec = 1000;
  tv2.tv_usec = 2;
  TEST_TVDIFF (tv1, tv2, 1);
  TEST_SUBTRACT (tv1, tv2, 0, 1);
  TEST_TVDIFF (tv2, tv1, -1);
  TEST_SUBTRACT (tv2, tv1, 0, -1);

  tv2.tv_sec = 1000;
  tv2.tv_usec = 3;
  TEST_TVDIFF (tv1, tv2, 2);
  TEST_SUBTRACT (tv1, tv2, 0, 2);
  TEST_TVDIFF (tv2, tv1, -2);
  TEST_SUBTRACT (tv2, tv1, 0, -2);

  tv2.tv_sec = 1001;
  tv2.tv_usec = 0;
  TEST_TVDIFF (tv1, tv2, 999999);
  TEST_SUBTRACT (tv1, tv2, 0, 999999);
  TEST_TVDIFF (tv2, tv1, -999999);
  TEST_SUBTRACT (tv2, tv1, 0, -999999);

  tv1.tv_sec = 1000;
  tv1.tv_usec = 999999;
  tv2.tv_sec = 1001;
  tv2.tv_usec = 1;
  TEST_TVDIFF (tv1, tv2, 2);
  TEST_SUBTRACT (tv1, tv2, 0, 2);
  TEST_TVDIFF (tv2, tv1, -2);
  TEST_SUBTRACT (tv2, tv1, 0, -2);

  tv1.tv_sec = 1000;
  tv1.tv_usec = 1;
  tv2.tv_sec = 1001;
  tv2.tv_usec = 2;
  TEST_TVDIFF (tv1, tv2, 1000001);
  TEST_SUBTRACT (tv1, tv2, 1, 1);
  TEST_TVDIFF (tv2, tv1, -1000001);
  TEST_SUBTRACT (tv2, tv1, -1, -1);

  /* Test that an arbitrary tv is equal to itself. */
  gettimeofday (&tv1, NULL);
  TEST_TVDIFF (tv1, tv1, 0);
  TEST_SUBTRACT (tv1, tv1, 0, 0);

  exit (errors == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
