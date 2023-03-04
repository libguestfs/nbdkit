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

#include <guestfs.h>

#include "iszero.h"
#include "test.h"

int
main (int argc, char *argv[])
{
  guestfs_h *g;
  int r;
  char *data;
  size_t size;

  if (test_start_nbdkit ("data",
                         /* This example from the nbdkit-data-plugin(1)
                          * man page creates a 1 MB disk with one
                          * empty MBR-formatted partition.
                          */
                         "@0x1b8 0xf8 0x21 0xdc 0xeb 0*4 "
                         "2 0 0x83 0x20*2 0 1 0  0 0 0xff 0x7 "
                         "@0x1fe 0x55 0xaa",
                         "size=1M",
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

  /* Read the actual data in the first sector, to check that it
   * matches what we described in the data= parameter above.
   */
  data = guestfs_pread_device (g, "/dev/sda", 512, 0, &size);
  if (data == NULL)
    exit (EXIT_FAILURE);
  if (size != 512) {
    fprintf (stderr, "%s: unexpected short read\n", program_name);
    exit (EXIT_FAILURE);
  }
  if (memcmp (&data[0x1b8],
              "\xf8\x21\xdc\xeb\0\0\0\0"
              "\2\0\x83\x20\x20\0\1\0\0\0\xff\x7", 20) != 0 ||
      memcmp (&data[0x1fe], "\x55\xaa", 2) != 0) {
    fprintf (stderr, "%s: unexpected data in boot sector\n", program_name);
    exit (EXIT_FAILURE);
  }
  memset (&data[0x1b8], 0, 20);
  memset (&data[0x1fe], 0, 2);
  if (!is_zero (data, 512)) {
    fprintf (stderr, "%s: unexpected data in zero parts of boot sector\n",
             program_name);
    exit (EXIT_FAILURE);
  }
  free (data);

  /* Since the disk image has a partition, we should be able to format it. */
  if (guestfs_mkfs (g, "vfat", "/dev/sda1") == -1)
    exit (EXIT_FAILURE);

  /* Mount it and write a file. */
  if (guestfs_mount (g, "/dev/sda1", "/") == -1)
    exit (EXIT_FAILURE);

  if (guestfs_write (g, "/foo", "hello", 5) == -1)
    exit (EXIT_FAILURE);

  if (guestfs_shutdown (g) == -1)
    exit (EXIT_FAILURE);

  guestfs_close (g);
  exit (EXIT_SUCCESS);
}
