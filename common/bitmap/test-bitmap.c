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

/* Unit tests of the bitmap code. */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#undef NDEBUG /* Keep test strong even for nbdkit built without assertions */
#include <assert.h>

#include <nbdkit-plugin.h>

#include "array-size.h"
#include "bitmap.h"

static void
test (int bpb, int blksize)
{
  struct bitmap bm;
  const int nr_blocks = 1000;
  int blks[] =
    {
     1, 2, 3, 10, 12,
     90, 91, 92, 93, 94, 99,
     800, 801, 802, 803,
     902, 903, 905, 907, 911, 913, 917, 919, 923, 929,
     999
    };
  unsigned v, vexp;
  size_t i, j;

  printf ("bpb = %d, blksize = %d\n", bpb, blksize);
  fflush (stdout);

  bitmap_init (&bm, blksize, bpb);
  if (bitmap_resize (&bm, nr_blocks * blksize) == -1)
    exit (EXIT_FAILURE);

  /* Set some bits at known block numbers. */
  for (j = 0; j < ARRAY_SIZE (blks); ++j) {
    v = (j & 1) == 0 ? 1 : (1<<bpb) - 1;
    bitmap_set_blk (&bm, blks[j], v);
  }

  /* Check the values of all bits. */
  for (i = j = 0; i < nr_blocks; ++i) {
    if (i == blks[j]) { /* previously set bit */
      vexp = (j & 1) == 0 ? 1 : (1<<bpb) - 1;
      v = bitmap_get_blk (&bm, blks[j], 0);
      assert (v == vexp);
      ++j;
    }
    else { /* unset bit, except it to be zero */
      v = bitmap_get_blk (&bm, i, 0);
      assert (v == 0);
    }
  }

  /* Same as above, but using bitmap_for macro. */
  j = 0;
  bitmap_for (&bm, i) {
    if (i == blks[j]) { /* previously set bit */
      vexp = (j & 1) == 0 ? 1 : (1<<bpb) - 1;
      v = bitmap_get_blk (&bm, blks[j], 0);
      assert (v == vexp);
      ++j;
    }
    else { /* unset bit, expect it to be zero */
      v = bitmap_get_blk (&bm, i, 0);
      assert (v == 0);
    }
  }

  /* Use bitmap_next to iterate over the non-zero entries in the bitmap. */
  i = bitmap_next (&bm, 0);
  j = 0;
  while (i != -1) {
    assert (i == blks[j]);
    vexp = (j & 1) == 0 ? 1 : (1<<bpb) - 1;
    v = bitmap_get_blk (&bm, i, 0);
    assert (v == vexp);
    i = bitmap_next (&bm, i+1);
    ++j;
  }

  bitmap_free (&bm);
}

int
main (void)
{
  int bpb;
  size_t i;
  int blksizes[] = { 1, 2, 4, 1024, 2048, 4096, 16384 };

  /* Try the tests at each bpb setting and at a range of block sizes. */
  for (bpb = 1; bpb <= 8; bpb <<= 1)
    for (i = 0; i < sizeof blksizes / sizeof blksizes[0]; ++i)
      test (bpb, blksizes[i]);

  exit (EXIT_SUCCESS);
}

/* The bitmap code uses nbdkit_debug, normally provided by the main
 * server program.  So we have to provide it here.
 */
void
nbdkit_debug (const char *fs, ...)
{
  /* do nothing */
}

/* Same for nbdkit_error. */
void
nbdkit_error (const char *fs, ...)
{
  int err = errno;
  va_list args;

  va_start (args, fs);
  fprintf (stderr, "error: ");
  errno = err; /* Must restore in case fs contains %m */
  vfprintf (stderr, fs, args);
  fprintf (stderr, "\n");
  va_end (args);

  errno = err;
}
