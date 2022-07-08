/* nbdkit
 * Copyright (C) 2013-2021 Red Hat Inc.
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
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>

#include <libnbd.h>

int
main (int argc, char *argv[])
{
  struct nbd_handle *nbd;
  char buf[1024];
  int r;

  /* Check "disk" was created before running the test. */
  if (access ("disk", R_OK) == -1 && errno == ENOENT) {
    printf ("%s: test skipped because \"disk\" was not created\n", argv[0]);
    exit (77);
  }

  nbd = nbd_create ();
  if (nbd == NULL) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  if (nbd_connect_command (nbd,
                           (char *[]) {
                             "nbdkit", "-s", "--exit-with-parent",
                             "-v", "-D", "protect.write=1",
                             "--filter=protect",
                             "--filter=cow",
                             "file", "disk",
                             "protect=~0x1be-",
                             NULL }) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  /* Read the first two sectors. */
  if (nbd_pread (nbd, buf, sizeof buf, 0, 0) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  /* Modifying the second sector should be possible. */
  buf[512] = 1;
  buf[513] = 2;
  buf[514] = 3;
  if (nbd_pwrite (nbd, buf, sizeof buf, 0, 0) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  /* Modifying the partition table should be possible. */
  buf[0x1be] = 1;
  buf[0x1bf] = 2;
  buf[0x1c0] = 3;
  if (nbd_pwrite (nbd, buf, sizeof buf, 0, 0) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  /* Modifying the beginning of the disk must return EPERM. */
  buf[0] = 1;
  buf[1] = 2;
  buf[2] = 3;
  r = nbd_pwrite (nbd, buf, sizeof buf, 0, 0);
  if (r != -1) {
    fprintf (stderr,
             "%s: protect filter did not protect boot sector\n",
             argv[0]);
    exit (EXIT_FAILURE);
  }
  if (nbd_get_errno () != EPERM) {
    fprintf (stderr,
             "%s: protect filter did not return EPERM error "
             "(instead: %s)\n",
             argv[0], nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  nbd_shutdown (nbd, 0);
  nbd_close (nbd);
  exit (EXIT_SUCCESS);
}
