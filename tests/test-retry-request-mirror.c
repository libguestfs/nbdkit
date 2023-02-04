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

/* This is a test of recovering from broken redirects to a mirror
 * service.  See the following bug for background:
 * https://bugzilla.redhat.com/show_bug.cgi?id=2013000
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include <libnbd.h>

#include "cleanup.h"
#include "web-server.h"

#include "test.h"

int
main (int argc, char *argv[])
{
  const char *sockpath;
  CLEANUP_FREE char *usp_param = NULL;
  int i, j;
  char state = 0;
  struct nbd_handle *nbd = NULL;

#ifndef HAVE_CURLOPT_UNIX_SOCKET_PATH
  fprintf (stderr, "%s: curl does not support CURLOPT_UNIX_SOCKET_PATH\n",
           argv[0]);
  exit (77);
#endif

  if (access ("disk", F_OK) == -1) {
    fprintf (stderr, "%s: 'disk' not built test skipped\n", argv[0]);
    exit (77);
  }

  sockpath = web_server ("disk" /* not used but must be set */, NULL);
  if (sockpath == NULL) {
    fprintf (stderr, "%s: could not start web server thread\n", argv[0]);
    exit (EXIT_FAILURE);
  }

  /* Start nbdkit. */
  if (asprintf (&usp_param, "unix-socket-path=%s", sockpath) == -1) {
    perror ("asprintf");
    exit (EXIT_FAILURE);
  }
  if (test_start_nbdkit ("--filter=retry-request",
                         "curl", usp_param, "http://localhost/mirror",
                         "retry-request-delay=1",
                         NULL) == -1)
    exit (EXIT_FAILURE);

  /* The way the test works is we fetch the magic "/mirror" path (see
   * web-server.c).  This redirects to /mirror1, /mirror2, /mirror3
   * round robin on each request.  /mirror1 returns all 1's, /mirror2
   * returns all 2's, and /mirror3 returns a 404 error.  The 404 error
   * should be transparently skipped by the filter, so we should see
   * alternating 1's and 2's buffers.
   */
  for (j = 0; j < 5; ++j) {
    /* Connect to the NBD socket. */
    nbd = nbd_create ();
    if (nbd == NULL)
      goto nbd_error;

    if (nbd_connect_unix (nbd, sock /* NBD socket */) == -1)
      goto nbd_error;

    for (i = 0; i < 7 /* not divisible by 2 or 3 */; ++i) {
      char buf[512];

      if (nbd_pread (nbd, buf, sizeof buf, 0, 0) == -1)
        goto nbd_error;

      if (state == 0) {
        /* First time, set the state to 1 or 2 depending on what
         * we just read.
         */
        state = buf[0];
      }
      else {
        /* Subsequent times, check that the mirror flipped to the
         * other state.
         */
        if (buf[0] != state || buf[1] != state || buf[511] != state) {
          fprintf (stderr, "%s: unexpected state: expecting %d but found %d\n",
                   argv[0], (int) state, (int) buf[0]);
          nbd_close (nbd);
          exit (EXIT_FAILURE);
        }
      }

      state++;
      if (state == 3)
        state = 1;
    }

    nbd_close (nbd);
  }

  exit (EXIT_SUCCESS);

 nbd_error:
  fprintf (stderr, "%s: %s\n", argv[0], nbd_get_error ());
  if (nbd) nbd_close (nbd);
  exit (EXIT_FAILURE);
}
