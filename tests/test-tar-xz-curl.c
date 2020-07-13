/* nbdkit
 * Copyright (C) 2013-2020 Red Hat Inc.
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

#include "cleanup.h"
#include "web-server.h"

#include "test.h"

int
main (int argc, char *argv[])
{
  static const char disk[] = "disk.tar.xz";
  const char *sockpath;
  guestfs_h *g;
  int r;
  char *data;
  CLEANUP_FREE char *usp_param = NULL;

  if (access (disk, F_OK) == -1) {
    fprintf (stderr, "%s: %s not found, test skipped\n", argv[0], disk);
    exit (77);
  }

#ifndef HAVE_CURLOPT_UNIX_SOCKET_PATH
  fprintf (stderr, "%s: curl does not support CURLOPT_UNIX_SOCKET_PATH\n",
           argv[0]);
  exit (77);
#endif

  sockpath = web_server (disk);
  if (sockpath == NULL) {
    fprintf (stderr, "test-tar-xz-curl: could not start web server thread\n");
    exit (EXIT_FAILURE);
  }

  /* Start nbdkit. */
  if (asprintf (&usp_param, "unix-socket-path=%s", sockpath) == -1) {
    perror ("asprintf");
    exit (EXIT_FAILURE);
  }
  if (test_start_nbdkit ("--filter=tar", "--filter=xz",
                         "curl", usp_param, "http://localhost/disk.tar.xz",
                         "tar-entry=disk",
                         NULL) == -1)
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

  /* disk contains one partition and a test file called "hello.txt" */
  if (guestfs_mount_ro (g, "/dev/sda1", "/") == -1)
    exit (EXIT_FAILURE);

  data = guestfs_cat (g, "/hello.txt");
  if (!data)
    exit (EXIT_FAILURE);

  if (strcmp (data, "hello,world") != 0) {
    fprintf (stderr,
             "%s FAILED: unexpected content of /hello.txt file "
             "(actual: %s, expected: \"hello,world\")\n",
             program_name, data);
    exit (EXIT_FAILURE);
  }

  guestfs_close (g);
  exit (EXIT_SUCCESS);
}
