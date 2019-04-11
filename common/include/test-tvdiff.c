/* nbdkit
 * Copyright (C) 2019 Red Hat Inc.
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
#include <assert.h>
#include <sys/time.h>

#include "tvdiff.h"

/* This is mainly a test of the compiler and platform rather than our
 * implementation.
 */

int
main (void)
{
  struct timeval tv1, tv2;

  tv1.tv_sec = 1000;
  tv1.tv_usec = 1;
  assert (tvdiff_usec (&tv1, &tv1) == 0);
  tv2.tv_sec = 1000;
  tv2.tv_usec = 2;
  assert (tvdiff_usec (&tv1, &tv2) == 1);
  assert (tvdiff_usec (&tv2, &tv1) == -1);
  tv2.tv_sec = 1000;
  tv2.tv_usec = 3;
  assert (tvdiff_usec (&tv1, &tv2) == 2);
  assert (tvdiff_usec (&tv2, &tv1) == -2);
  tv2.tv_sec = 1001;
  tv2.tv_usec = 0;
  assert (tvdiff_usec (&tv1, &tv2) == 999999);
  assert (tvdiff_usec (&tv2, &tv1) == -999999);

  tv1.tv_sec = 1000;
  tv1.tv_usec = 999999;
  tv2.tv_sec = 1001;
  tv2.tv_usec = 1;
  assert (tvdiff_usec (&tv1, &tv2) == 2);
  assert (tvdiff_usec (&tv2, &tv1) == -2);

  /* Test that an arbitrary tv is equal to itself. */
  gettimeofday (&tv1, NULL);
  assert (tvdiff_usec (&tv1, &tv1) == 0);

  exit (EXIT_SUCCESS);
}
