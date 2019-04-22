/* nbdkit
 * Copyright (C) 2017 Red Hat Inc.
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

#include <guestfs.h>

#include "test.h"

static char data[257] =
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

int
main (int argc, char *argv[])
{
  guestfs_h *g;
  int r;
  char *rdata;
  size_t rsize;
  size_t i;

  if (test_start_nbdkit ("null", "100M", NULL) == -1)
    exit (EXIT_FAILURE);

  g = guestfs_create ();
  if (g == NULL) {
    perror ("guestfs_create");
    exit (EXIT_FAILURE);
  }

  r = guestfs_add_drive_opts (g, "",
                              GUESTFS_ADD_DRIVE_OPTS_FORMAT, "raw",
                              GUESTFS_ADD_DRIVE_OPTS_PROTOCOL, "nbd",
                              GUESTFS_ADD_DRIVE_OPTS_SERVER, server,
                              -1);
  if (r == -1)
    exit (EXIT_FAILURE);

  if (guestfs_launch (g) == -1)
    exit (EXIT_FAILURE);

  /* All writes should read back as zeroes.  However we have to
   * reopen the handle to stop the libguestfs kernel from caching
   * the written data.
   */
  if (guestfs_pwrite_device (g, "/dev/sda", data, 256, 1024) == -1)
    exit (EXIT_FAILURE);

  if (guestfs_shutdown (g) == -1)
    exit (EXIT_FAILURE);

  guestfs_close (g);

  g = guestfs_create ();
  if (g == NULL) {
    perror ("guestfs_create");
    exit (EXIT_FAILURE);
  }

  r = guestfs_add_drive_opts (g, "",
                              GUESTFS_ADD_DRIVE_OPTS_FORMAT, "raw",
                              GUESTFS_ADD_DRIVE_OPTS_PROTOCOL, "nbd",
                              GUESTFS_ADD_DRIVE_OPTS_SERVER, server,
                              -1);
  if (r == -1)
    exit (EXIT_FAILURE);

  if (guestfs_launch (g) == -1)
    exit (EXIT_FAILURE);

  rdata = guestfs_pread_device (g, "/dev/sda", 2048, 0, &rsize);
  if (rdata == NULL)
    exit (EXIT_FAILURE);
  if (rsize != 2048) {
    fprintf (stderr, "test-null: short read\n");
    exit (EXIT_FAILURE);
  }

  for (i = 0; i < 2048; ++i) {
    if (rdata[i] != 0) {
      fprintf (stderr, "test-null: unexpected non-zero data read\n");
      exit (EXIT_FAILURE);
    }
  }
  free (rdata);

  if (guestfs_shutdown (g) == -1)
    exit (EXIT_FAILURE);

  guestfs_close (g);
  exit (EXIT_SUCCESS);
}
