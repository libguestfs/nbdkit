/* nbdkit
 * Copyright (C) 2018 Red Hat Inc.
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

#include "test.h"

int
main (int argc, char *argv[])
{
  guestfs_h *g;
  int r;
  FILE *fp;
  char *data;
#if 0
  int error;
#endif
  char tmpdir[] = "/tmp/errorXXXXXX";
  char error_file[] = "/tmp/errorXXXXXX/trigger";
  char error_file_param[] = "error-file=/tmp/errorXXXXXX/trigger";

  /* Create temporary directory to store the trigger file. */
  if (mkdtemp (tmpdir) == NULL) {
    perror ("mkdtemp");
    exit (EXIT_FAILURE);
  }

  memcpy (error_file, tmpdir, strlen (tmpdir));
  memcpy (&error_file_param[11], tmpdir, strlen (tmpdir));

  if (test_start_nbdkit ("--filter", "error",
                         "memory", "1M",
                         "error=EIO",
                         "error-rate=100%",
                         error_file_param,
                         NULL) == -1)
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

  /* Format the disk with a filesystem.  No errors are being injected
   * yet so we expect this to work.
   */
  if (guestfs_part_disk (g, "/dev/sda", "mbr") == -1)
    exit (EXIT_FAILURE);

  if (guestfs_mkfs (g, "ext2", "/dev/sda1") == -1)
    exit (EXIT_FAILURE);

  if (guestfs_mount (g, "/dev/sda1", "/") == -1)
    exit (EXIT_FAILURE);

#define filename "/hello.txt"
#define content "hello, people of the world"

  if (guestfs_write (g, filename, content, strlen (content)) == -1)
    exit (EXIT_FAILURE);

  /* Try as hard as we can to sync data and kill the libguestfs cache. */
  if (guestfs_sync (g) == -1)
    exit (EXIT_FAILURE);
  if (guestfs_drop_caches (g, 3) == -1)
    exit (EXIT_FAILURE);
  sleep (1);

  /* Now start injecting EIO errors. */
  fp = fopen (error_file, "w");
  if (fp == NULL) {
    perror (error_file);
    exit (EXIT_FAILURE);
  }
  fclose (fp);

  data = guestfs_cat (g, filename);
  if (data != NULL) {
    fprintf (stderr,
             "%s: error: "
             "expecting Input/output error, but read data!\n",
             program_name);
    exit (EXIT_FAILURE);
  }

#if 0
  /* Apparently libguestfs doesn't preserve the errno here yet XXX */
  error = guestfs_last_errno (g);
  if (error != EIO) {
    fprintf (stderr, "%s: error: expecting errno = EIO, but got %d\n",
             program_name, error);
    exit (EXIT_FAILURE);
  }
#endif

  /* Stop injecting errors, hope that the filesystem recovers. */
  unlink (error_file);

  /* But we'll probably have to remount the filesystem because ext2
   * will get itself into a "state".
   */
  if (guestfs_umount (g, "/") == -1)
    exit (EXIT_FAILURE);
  if (guestfs_mount (g, "/dev/sda1", "/") == -1)
    exit (EXIT_FAILURE);

  data = guestfs_cat (g, filename);
  if (data == NULL)
    exit (EXIT_FAILURE);
  if (strcmp (data, content) != 0) {
    fprintf (stderr, "%s: error: read unexpected data\n", program_name);
    exit (EXIT_FAILURE);
  }

  guestfs_close (g);

  rmdir (tmpdir);

  exit (EXIT_SUCCESS);
}
