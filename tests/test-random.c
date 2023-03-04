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
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <libnbd.h>

#include "random.h"

#define SIZE 1024*1024
static char data[SIZE];

static unsigned histogram[256];

/* After reading the whole disk above, we then read randomly selected
 * subsets of the disk and compare the data (it should be identical).
 */
#define RSIZE 10240
#define NR_READS 50
static char rdata[RSIZE];

int
main (int argc, char *argv[])
{
  struct nbd_handle *nbd;
  struct random_state random_state;
  int i;
  unsigned offset;
  char sizearg[32];

  nbd = nbd_create ();
  if (nbd == NULL) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  snprintf (sizearg, sizeof sizearg, "%d", SIZE);
  if (nbd_connect_command (nbd,
                           (char *[]) {
                             "nbdkit", "-s", "--exit-with-parent",
                             "random", sizearg,
                             NULL }) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  /* Read the whole device. */
  if (nbd_pread (nbd, data, sizeof data, 0, 0) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  /* Test that the data is sufficiently random using a simple
   * histogram.  This just tests for gross errors and is not a
   * complete statistical study.
   */
  for (i = 0; i < SIZE; ++i) {
    unsigned char c = (unsigned char) data[i];
    histogram[c]++;
  }
  for (i = 0; i < 256; ++i) {
    const unsigned expected = SIZE / 256;
    if (histogram[i] < 80 * expected / 100) {
      fprintf (stderr, "test-random: "
               "random data is not uniformly distributed\n"
               "eg. byte %d occurs %u times (expected about %u times)\n",
               i, histogram[i], expected);
      exit (EXIT_FAILURE);
    }
  }

  /* Randomly read parts of the disk to ensure we get the same data.
   */
  xsrandom (time (NULL), &random_state);
  for (i = 0; i < NR_READS; ++i) {
    offset = xrandom (&random_state);
    offset %= SIZE - RSIZE;
    if (nbd_pread (nbd, rdata, sizeof rdata, offset, 0) == -1) {
      fprintf (stderr, "%s\n", nbd_get_error ());
      exit (EXIT_FAILURE);
    }
    if (memcmp (&data[offset], rdata, RSIZE) != 0) {
      fprintf (stderr, "test-random: returned different data\n");
      exit (EXIT_FAILURE);
    }
  }

  nbd_close (nbd);
  exit (EXIT_SUCCESS);
}
