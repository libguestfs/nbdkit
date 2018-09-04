/* nbdkit
 * Copyright (C) 2013-2018 Red Hat Inc.
 * All rights reserved.
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
#include <sys/types.h>

#include <guestfs.h>

#include "test.h"

static char *loopdev;                   /* Name of the loop device. */
static void detach_loopdev (void);

int
main (int argc, char *argv[])
{
  guestfs_h *g;
  int r;
  int fd;
  char cmd[64], buf[64];
  char disk[] = "/tmp/diskXXXXXX"; /* Backing disk. */
  FILE *pp;
  char *data;
  size_t len;
  char *s;

  /* This test can only be run as root, and will be skipped otherwise. */
  if (geteuid () != 0) {
    fprintf (stderr, "%s: this test has to be run as root.\n",
             program_name);
    exit (77);
  }

  /* losetup must be available. */
  r = system ("losetup --version");
  if (r != 0) {
    fprintf (stderr, "%s: losetup program must be installed.\n",
             program_name);
    exit (77);
  }

  /* Create the temporary backing disk. */
  fd = mkstemp (disk);
  if (fd == -1) {
    perror ("mkstemp");
    exit (EXIT_FAILURE);
  }
  if (ftruncate (fd, 100 * 1024 * 1024) == -1) {
    perror ("ftruncate");
    unlink (disk);
    exit (EXIT_FAILURE);
  }

  /* Create the loopback device. */
  snprintf (cmd, sizeof cmd, "losetup -f --show %s", disk);
  pp = popen (cmd, "r");
  if (pp == NULL) {
    perror ("popen: losetup");
    unlink (disk);
    exit (EXIT_FAILURE);
  }
  if (fgets (buf, sizeof buf, pp) == NULL) {
    fprintf (stderr, "%s: could not read loop device name from losetup\n",
             program_name);
    unlink (disk);
    exit (EXIT_FAILURE);
  }
  len = strlen (buf);
  if (len > 0 && buf[len-1] == '\n') {
    buf[len-1] = '\0';
    len--;
  }
  pclose (pp);

  /* We can delete the backing disk.  The loop device will hold it open. */
  unlink (disk);

  /* If we get to this point, set up an atexit handler to detach the
   * loop device.
   */
  loopdev = malloc (len+1);
  if (loopdev == NULL) {
    perror ("malloc");
    exit (EXIT_FAILURE);
  }
  strcpy (loopdev, buf);
  atexit (detach_loopdev);

  /* Start nbdkit. */
  snprintf (buf, sizeof buf, "file=%s", loopdev);
  if (test_start_nbdkit ("-D", "file.zero=1",
                         "file", buf, NULL) == -1)
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

  /* Partition the disk. */
  if (guestfs_part_disk (g, "/dev/sda", "mbr") == -1)
    exit (EXIT_FAILURE);
  if (guestfs_mkfs (g, "ext4", "/dev/sda1") == -1)
    exit (EXIT_FAILURE);

  if (guestfs_mount_options (g, "discard", "/dev/sda1", "/") == -1)
    exit (EXIT_FAILURE);

#define filename "/hello.txt"
#define content "hello, people of the world"

  if (guestfs_write (g, filename, content, strlen (content)) == -1)
    exit (EXIT_FAILURE);

  data = guestfs_cat (g, filename);
  if (!data)
    exit (EXIT_FAILURE);

  if (strcmp (data, content) != 0) {
    fprintf (stderr, "%s FAILED: unexpected content of %s file (actual: %s, expected: %s)\n",
             program_name, filename, data, content);
    exit (EXIT_FAILURE);
  }

  /* Run sync to test flush path. */
  if (guestfs_sync (g) == -1)
    exit (EXIT_FAILURE);

  /* Run fstrim to test trim path.  However only recent versions of
   * libguestfs have this, and it probably only works in recent
   * versions of qemu.
   */
#ifdef GUESTFS_HAVE_FSTRIM
  if (guestfs_fstrim (g, "/", -1) == -1)
    exit (EXIT_FAILURE);
#endif

  /* Run fallocate(1) on the device to test zero path. */
  if (guestfs_umount (g, "/") == -1)
    exit (EXIT_FAILURE);
  const char *sh[] = { "fallocate", "-nzl", "64k", "/dev/sda", NULL };
  s = guestfs_debug (g, "sh", (char **) sh);
  free (s);

  if (guestfs_shutdown (g) == -1)
    exit (EXIT_FAILURE);

  guestfs_close (g);

  /* The atexit handler should detach the loop device and clean up
   * the backing disk.
   */
  exit (EXIT_SUCCESS);
}

/* atexit handler. */
static void
detach_loopdev (void)
{
  char cmd[64];

  if (loopdev == NULL)
    return;

  snprintf (cmd, sizeof cmd, "losetup -d %s", loopdev);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
  system (cmd);
#pragma GCC diagnostic pop
  free (loopdev);
  loopdev = NULL;
}
