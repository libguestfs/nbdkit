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

#include <libnbd.h>

#include "cleanup.h"
#include "web-server.h"

#include "test.h"

static int iteration;

#define SCRIPT \
  "if [ $iteration -eq 0 ]; then echo X-Test: hello; fi\n" \
  "echo X-Iteration: $iteration\n" \
  "echo 'X-Empty;'\n"

static void
check_request (const char *request)
{
  char expected[64];

  /* Check the iteration header. */
  snprintf (expected, sizeof expected, "\r\nX-Iteration: %u\r\n", iteration);
  if (strcasestr (request, expected) == NULL) {
    fprintf (stderr, "%s: no/incorrect X-Iteration header in request\n",
             program_name);
    exit (EXIT_FAILURE);
  }

  /* Check the test header, only sent when $iteration = 0. */
  if (iteration == 0) {
    if (strcasestr (request, "\r\nX-Test: hello\r\n") == NULL) {
      fprintf (stderr, "%s: no X-Test header in request\n", program_name);
      exit (EXIT_FAILURE);
    }
  }
  else {
    if (strcasestr (request, "\r\nX-Test:") != NULL) {
      fprintf (stderr, "%s: X-Test header sent but not expected\n",
               program_name);
      exit (EXIT_FAILURE);
    }
  }

  /* Check the empty header. */
  if (strcasestr (request, "\r\nX-Empty:\r\n") == NULL) {
    fprintf (stderr, "%s: no X-Empty header in request\n", program_name);
    exit (EXIT_FAILURE);
  }
}

static char buf[512];

int
main (int argc, char *argv[])
{
  const char *sockpath;
  struct nbd_handle *nbd;
  CLEANUP_FREE char *usp_param = NULL;

#ifndef HAVE_CURLOPT_UNIX_SOCKET_PATH
  fprintf (stderr, "%s: curl does not support CURLOPT_UNIX_SOCKET_PATH\n",
           program_name);
  exit (77);
#endif

  sockpath = web_server ("disk", check_request);
  if (sockpath == NULL) {
    fprintf (stderr, "%s: could not start web server thread\n", program_name);
    exit (EXIT_FAILURE);
  }

  nbd = nbd_create ();
  if (nbd == NULL) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  /* We expect that connecting will cause a HEAD request (to find the
   * size).  $iteration will be 0.
   */
  iteration = 0;

  /* Start nbdkit. */
  if (asprintf (&usp_param, "unix-socket-path=%s", sockpath) == -1) {
    perror ("asprintf");
    exit (EXIT_FAILURE);
  }
  char *args[] = {
    "nbdkit", "-s", "--exit-with-parent", "-v",
    "curl",
    "-D", "curl.verbose=1",
    "http://localhost/disk",
    "header-script=" SCRIPT,
    "header-script-renew=1",
    usp_param, /* unix-socket-path=... */
    NULL
  };
  if (nbd_connect_command (nbd, args) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  /* Sleep the script will be called again.  $iteration will be 1. */
  sleep (2);
  iteration = 1;

  /* Make a request. */
  if (nbd_pread (nbd, buf, sizeof buf, 0, 0) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  /* Sleep again and make another request.  $iteration will be 2. */
  sleep (2);
  iteration = 2;

  if (nbd_pread (nbd, buf, sizeof buf, 0, 0) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  nbd_close (nbd);
  exit (EXIT_SUCCESS);
}
