/* nbdkit
 * Copyright (C) 2019 Red Hat Inc.
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
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include "internal.h"

/* Handle the --run option.  If run is NULL, does nothing.  If run is
 * not NULL then run nbdkit as a captive subprocess of the command.
 */
void
run_command (void)
{
  char *url;
  char *cmd;
  int r;
  pid_t pid;

  if (!run)
    return;

  /* Construct an nbd "URL".  Unfortunately guestfish and qemu take
   * different syntax, so try to guess which one we need.
   */
  if (strstr (run, "guestfish")) {
    if (port)
      r = asprintf (&url, "nbd://localhost:%s", port);
    else if (unixsocket)
      /* XXX escaping? */
      r = asprintf (&url, "nbd://?socket=%s", unixsocket);
    else
      abort ();
  }
  else /* qemu */ {
    if (port)
      r = asprintf (&url, "nbd:localhost:%s", port);
    else if (unixsocket)
      r = asprintf (&url, "nbd:unix:%s", unixsocket);
    else
      abort ();
  }
  if (r == -1) {
    perror ("asprintf");
    exit (EXIT_FAILURE);
  }

  /* Construct the final command including shell variables. */
  /* XXX Escaping again. */
  r = asprintf (&cmd,
                "nbd='%s'\n"
                "port='%s'\n"
                "unixsocket='%s'\n"
                "%s",
                url, port ? port : "", unixsocket ? unixsocket : "", run);
  if (r == -1) {
    perror ("asprintf");
    exit (EXIT_FAILURE);
  }

  free (url);

  /* Fork.  Captive nbdkit runs as the child process. */
  pid = fork ();
  if (pid == -1) {
    perror ("fork");
    exit (EXIT_FAILURE);
  }

  if (pid > 0) {              /* Parent process is the run command. */
    r = system (cmd);
    if (WIFEXITED (r))
      r = WEXITSTATUS (r);
    else if (WIFSIGNALED (r)) {
      fprintf (stderr, "%s: external command was killed by signal %d\n",
               program_name, WTERMSIG (r));
      r = 1;
    }
    else if (WIFSTOPPED (r)) {
      fprintf (stderr, "%s: external command was stopped by signal %d\n",
               program_name, WSTOPSIG (r));
      r = 1;
    }

    kill (pid, SIGTERM);        /* Kill captive nbdkit. */

    _exit (r);
  }

  free (cmd);

  debug ("forked into background (new pid = %d)", getpid ());
}
