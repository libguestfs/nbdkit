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
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include <guestfs.h>

#include "test.h"

int
main (int argc, char *argv[])
{
  guestfs_h *g1, *g2;
  int r;
  char *label;

  /* Start nbdkit. */
  if (test_start_nbdkit ("tmpdisk", "1G", "label=TEST", NULL) == -1)
    exit (EXIT_FAILURE);

  /* We can open multiple connections and they should see different
   * disks.
   */
  g1 = guestfs_create ();
  if (g1 == NULL) {
    perror ("guestfs_create");
    exit (EXIT_FAILURE);
  }
  guestfs_set_identifier (g1, "g1");

  r = guestfs_add_drive_opts (g1, "",
                              GUESTFS_ADD_DRIVE_OPTS_FORMAT, "raw",
                              GUESTFS_ADD_DRIVE_OPTS_PROTOCOL, "nbd",
                              GUESTFS_ADD_DRIVE_OPTS_SERVER, server,
                              -1);
  if (r == -1)
    exit (EXIT_FAILURE);

  if (guestfs_launch (g1) == -1)
    exit (EXIT_FAILURE);

  g2 = guestfs_create ();
  if (g2 == NULL) {
    perror ("guestfs_create");
    exit (EXIT_FAILURE);
  }
  guestfs_set_identifier (g2, "g2");

  r = guestfs_add_drive_opts (g2, "",
                              GUESTFS_ADD_DRIVE_OPTS_FORMAT, "raw",
                              GUESTFS_ADD_DRIVE_OPTS_PROTOCOL, "nbd",
                              GUESTFS_ADD_DRIVE_OPTS_SERVER, server,
                              -1);
  if (r == -1)
    exit (EXIT_FAILURE);

  if (guestfs_launch (g2) == -1)
    exit (EXIT_FAILURE);

  /* But they should both see the same filesystem label. */
  label = guestfs_vfs_label (g1, "/dev/sda");
  if (!label)
    exit (EXIT_FAILURE);
  if (strcmp (label, "TEST") != 0) {
    fprintf (stderr, "%s FAILED: unexpected label: %s\n",
             program_name, label);
    exit (EXIT_FAILURE);
  }
  free (label);

  label = guestfs_vfs_label (g2, "/dev/sda");
  if (!label)
    exit (EXIT_FAILURE);
  if (strcmp (label, "TEST") != 0) {
    fprintf (stderr, "%s FAILED: unexpected label: %s\n",
             program_name, label);
    exit (EXIT_FAILURE);
  }
  free (label);

  /* Mount both disks. */
  if (guestfs_mount (g1, "/dev/sda", "/") == -1)
    exit (EXIT_FAILURE);
  if (guestfs_mount (g2, "/dev/sda", "/") == -1)
    exit (EXIT_FAILURE);

  /* Create some files and directories on each. */
  if (guestfs_mkdir (g1, "/test1") == -1)
    exit (EXIT_FAILURE);
  if (guestfs_touch (g1, "/test1/file1") == -1)
    exit (EXIT_FAILURE);
  if (guestfs_mkdir (g2, "/test2") == -1)
    exit (EXIT_FAILURE);
  if (guestfs_touch (g2, "/test2/file2") == -1)
    exit (EXIT_FAILURE);

  if (guestfs_sync (g1) == -1 || guestfs_sync (g2) == -1)
    exit (EXIT_FAILURE);

  if (guestfs_is_file (g1, "/test1/file1") != 1) {
    fprintf (stderr, "%s FAILED: /test1/file1 is not a file\n",
             program_name);
    exit (EXIT_FAILURE);
  }
  if (guestfs_is_file (g2, "/test2/file2") != 1) {
    fprintf (stderr, "%s FAILED: /test2/file2 is not a file\n",
             program_name);
    exit (EXIT_FAILURE);
  }

  /* Shut down the connection. */
  if (guestfs_shutdown (g1) == -1)
    exit (EXIT_FAILURE);
  if (guestfs_shutdown (g2) == -1)
    exit (EXIT_FAILURE);
  guestfs_close (g1);
  guestfs_close (g2);

  exit (EXIT_SUCCESS);
}
