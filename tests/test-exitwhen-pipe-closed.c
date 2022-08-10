/* nbdkit
 * Copyright (C) 2017-2020 Red Hat Inc.
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
#include <unistd.h>
#include <signal.h>

int
main (int argc, char *argv[])
{
  char *param;
  int fd[2];
  pid_t pid;

#ifdef WIN32
  fprintf (stderr, "%s: test skipped on Windows: no pipe(2)\n", argv[0]);
  exit (77);
#endif

#ifdef __APPLE__
  /* XXX Fix this. */
  fprintf (stderr, "%s: test skipped on macOS because the test hangs\n",
           argv[0]);
  exit (77);
#endif

  if (pipe (fd) == -1) {
    perror ("pipe");
    exit (EXIT_FAILURE);
  }
  if (asprintf (&param, "exit-when-pipe-closed=%d", fd[0]) == -1) {
    perror ("asprintf");
    exit (EXIT_FAILURE);
  }

  /* Run nbdkit. */
  pid = fork ();
  if (pid == -1) {
    perror ("fork");
    exit (EXIT_FAILURE);
  }
  if (pid == 0) {               /* Child - run nbdkit. */
    /* Close the write side of the pipe. */
    close (fd[1]);

    /* Run nbdkit. */
    execlp ("nbdkit", "nbdkit", "-v", "--filter=exitwhen",
            "null", "1M", param, "exit-when-poll=1",
            NULL);
    perror ("execvp");
    _exit (EXIT_FAILURE);
  }
  free (param);

  /* Close the read side of the pipe (this does NOT cause nbdkit to exit). */
  close (fd[0]);

  /* Wait a bit. */
  sleep (2);

  /* nbdkit should still be running. */
  if (kill (pid, 0) != 0) {
    fprintf (stderr, "FAIL: %s: nbdkit exited before pipe was closed\n",
             argv[0]);
    exit (EXIT_FAILURE);
  }

  /* The test here is simply that nbdkit exits because we exit and our
   * side of the pipe is closed.
   */
  exit (EXIT_SUCCESS);
}
