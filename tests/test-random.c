/* nbdkit
 * Copyright (C) 2017-2018 Red Hat Inc.
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

#include <guestfs.h>

#include "random.h"

#include "test.h"

#define SIZE 1024*1024
static char data[SIZE];

static unsigned histogram[256];

/* After reading the whole disk above, we then read randomly selected
 * subsets of the disk and compare the data (it should be identical).
 */
#define RSIZE 10240
#define NR_READS 50

int
main (int argc, char *argv[])
{
  guestfs_h *g;
  int r, i;
  struct random_state random_state;
  unsigned offset;
  char sizearg[32];
  char *rdata;
  size_t rsize;

  snprintf (sizearg, sizeof sizearg, "size=%d", SIZE);

  if (test_start_nbdkit ("random", sizearg, NULL) == -1)
    exit (EXIT_FAILURE);

  g = guestfs_create ();
  if (g == NULL) {
    perror ("guestfs_create");
    exit (EXIT_FAILURE);
  }

  r = guestfs_add_drive_opts (g, "",
                              GUESTFS_ADD_DRIVE_OPTS_READONLY, 1,
                              GUESTFS_ADD_DRIVE_OPTS_FORMAT, "raw",
                              GUESTFS_ADD_DRIVE_OPTS_PROTOCOL, "nbd",
                              GUESTFS_ADD_DRIVE_OPTS_SERVER, server,
                              -1);
  if (r == -1)
    exit (EXIT_FAILURE);

  if (guestfs_launch (g) == -1)
    exit (EXIT_FAILURE);

  /* Read the whole device. */
  rdata = guestfs_pread_device (g, "/dev/sda", SIZE, 0, &rsize);
  if (rdata == NULL)
    exit (EXIT_FAILURE);
  if (rsize != SIZE) {
    fprintf (stderr, "test-random: short read\n");
    exit (EXIT_FAILURE);
  }
  memcpy (data, rdata, SIZE);
  free (rdata);

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
    rdata = guestfs_pread_device (g, "/dev/sda", RSIZE, offset, &rsize);
    if (rdata == NULL)
      exit (EXIT_FAILURE);
    if (rsize != RSIZE) {
      fprintf (stderr, "test-random: short read\n");
      exit (EXIT_FAILURE);
    }
    if (memcmp (&data[offset], rdata, rsize) != 0) {
      fprintf (stderr, "test-random: returned different data\n");
      exit (EXIT_FAILURE);
    }
    free (rdata);
  }

  if (guestfs_shutdown (g) == -1)
    exit (EXIT_FAILURE);

  guestfs_close (g);
  exit (EXIT_SUCCESS);
}
