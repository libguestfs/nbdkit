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
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <guestfs.h>

#include "test.h"

#define FILENAME "offset-data"
#define FILESIZE (10 * 1024 * 1024)

static void
create_file (void)
{
  FILE *fp;
  char pattern[512];
  size_t i;

  for (i = 0; i < sizeof pattern; i += 2) {
    pattern[i] = 0x55;
    pattern[i+1] = 0xAA;
  }

  fp = fopen (FILENAME, "w");
  if (fp == NULL) {
    perror (FILENAME);
    exit (EXIT_FAILURE);
  }

  for (i = 0; i < FILESIZE; i += sizeof pattern) {
    if (fwrite (pattern, sizeof pattern, 1, fp) != 1) {
      perror (FILENAME ": write");
      exit (EXIT_FAILURE);
    }
  }

  if (fclose (fp) == EOF) {
    perror (FILENAME);
    exit (EXIT_FAILURE);
  }
}

static uint8_t buf[1024*1024];

static void
check_buf (void)
{
  size_t i;

  for (i = 0; i < sizeof buf; i += 2) {
    if (buf[i] != 0x55 || buf[i+1] != 0xAA) {
      fprintf (stderr, "%s FAILED: file overwritten outside offset/range\n",
               program_name);
      exit (EXIT_FAILURE);
    }
  }
}

int
main (int argc, char *argv[])
{
  guestfs_h *g;
  int r, fd;
  char *data;

  /* FILENAME is a 10 MB file containing test pattern data 0x55AA
   * repeated.  We use the middle 8 MB to create a partition table and
   * filesystem, and check afterwards that the test pattern in the
   * first and final megabyte has not been overwritten.
   */
  create_file ();

  if (test_start_nbdkit ("--filter", "offset",
                         "file", FILENAME,
                         "offset=1M", "range=8M",
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

  data = guestfs_cat (g, filename);
  if (!data)
    exit (EXIT_FAILURE);

  if (strcmp (data, content) != 0) {
    fprintf (stderr,
             "%s FAILED: unexpected content of %s file "
             "(actual: %s, expected: %s)\n",
             program_name, filename, data, content);
    exit (EXIT_FAILURE);
  }

  if (guestfs_fill_dir (g, "/", 1000) == -1)
    exit (EXIT_FAILURE);

  if (guestfs_shutdown (g) == -1)
    exit (EXIT_FAILURE);

  guestfs_close (g);

  /* Check the first and final megabyte of test patterns has not been
   * overwritten in the underlying file.
   */
  fd = open (FILENAME, O_RDONLY|O_CLOEXEC);
  if (fd == -1) {
    perror (FILENAME ": open");
    exit (EXIT_FAILURE);
  }
  if (pread (fd, buf, sizeof buf, 0) != sizeof buf) {
    fprintf (stderr, "%s: pread: short read or error (%d)\n",
             FILENAME, errno);
    exit (EXIT_FAILURE);
  }
  check_buf ();
  if (pread (fd, buf, sizeof buf, 9*1024*1024) != sizeof buf) {
    fprintf (stderr, "%s: pread: short read or error (%d)\n",
             FILENAME, errno);
    exit (EXIT_FAILURE);
  }
  check_buf ();
  close (fd);

  unlink (FILENAME);

  exit (EXIT_SUCCESS);
}
