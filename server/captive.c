/* nbdkit
 * Copyright (C) 2019-2020 Red Hat Inc.
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
#include <signal.h>
#include <assert.h>

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include "utils.h"

#include "internal.h"

#ifndef WIN32

/* Handle the --run option.  If run is NULL, does nothing.  If run is
 * not NULL then run nbdkit as a captive subprocess of the command.
 */
void
run_command (void)
{
  FILE *fp;
  char *cmd = NULL;
  size_t len = 0;
  int r, status;
  pid_t pid;

  if (!run)
    return;

  if (!export_name)
    export_name = "";

  fp = open_memstream (&cmd, &len);
  if (fp == NULL) {
    perror ("open_memstream");
    exit (EXIT_FAILURE);
  }

  /* Construct $uri. */
  fprintf (fp, "uri=");
  if (port) {
    fprintf (fp, "nbd://localhost:");
    shell_quote (port, fp);
    if (strcmp (export_name, "") != 0) {
      putc ('/', fp);
      uri_quote (export_name, fp);
    }
  }
  else if (unixsocket) {
    fprintf (fp, "nbd+unix://");
    if (strcmp (export_name, "") != 0) {
      putc ('/', fp);
      uri_quote (export_name, fp);
    }
    fprintf (fp, "\\?socket=");
    uri_quote (unixsocket, fp);
  }
  putc ('\n', fp);

  /* Expose $exportname. */
  fprintf (fp, "exportname=");
  shell_quote (export_name, fp);
  putc ('\n', fp);

  /* Construct older $nbd "URL".  Unfortunately guestfish and qemu take
   * different syntax, so try to guess which one we need.
   */
  fprintf (fp, "nbd=");
  if (strstr (run, "guestfish")) {
    if (port) {
      fprintf (fp, "nbd://localhost:");
      shell_quote (port, fp);
    }
    else if (unixsocket) {
      fprintf (fp, "nbd://\\?socket=");
      shell_quote (unixsocket, fp);
    }
    else
      abort ();
  }
  else /* qemu */ {
    if (port) {
      fprintf (fp, "nbd:localhost:");
      shell_quote (port, fp);
    }
    else if (unixsocket) {
      fprintf (fp, "nbd:unix:");
      shell_quote (unixsocket, fp);
    }
    else
      abort ();
  }
  putc ('\n', fp);

  /* Construct $port and $unixsocket. */
  fprintf (fp, "port=");
  if (port)
    shell_quote (port, fp);
  putc ('\n', fp);
  fprintf (fp, "unixsocket=");
  if (unixsocket)
    shell_quote (unixsocket, fp);
  fprintf (fp, "\n");

  /* Add the --run command.  Note we don't have to quote this. */
  fprintf (fp, "%s", run);

  if (fclose (fp) == EOF) {
    perror ("memstream failed");
    exit (EXIT_FAILURE);
  }

  /* Fork.  Captive nbdkit runs as the child process. */
  pid = fork ();
  if (pid == -1) {
    perror ("fork");
    exit (EXIT_FAILURE);
  }

  if (pid > 0) {              /* Parent process is the run command. */
    /* Restore original stdin/out */
    if (dup2 (saved_stdin, STDIN_FILENO) == -1 ||
        dup2 (saved_stdout, STDOUT_FILENO) == -1) {
      r = -1;
    }
    else
      r = system (cmd);
    if (r == -1) {
      nbdkit_error ("failure to execute external command: %m");
      r = EXIT_FAILURE;
    }
    else if (WIFEXITED (r))
      r = WEXITSTATUS (r);
    else {
      assert (WIFSIGNALED (r));
      fprintf (stderr, "%s: external command was killed by signal %d\n",
               program_name, WTERMSIG (r));
      r = WTERMSIG (r) + 128;
    }

    switch (waitpid (pid, &status, WNOHANG)) {
    case -1:
      nbdkit_error ("waitpid: %m");
      r = EXIT_FAILURE;
      break;
    case 0:
      /* Captive nbdkit is still running; kill it.  We want to wait
       * for nbdkit to exit since that ensures all cleanup is done in
       * the plugin before we return.  However we don't care if nbdkit
       * returns an error, the exit code we return always comes from
       * the --run command.
       */
      kill (pid, SIGTERM);
      waitpid (pid, NULL, 0);
      break;
    default:
      /* Captive nbdkit exited unexpectedly; update the exit status. */
      if (WIFEXITED (status)) {
        if (r == 0)
          r = WEXITSTATUS (status);
      }
      else {
        assert (WIFSIGNALED (status));
        fprintf (stderr, "%s: nbdkit command was killed by signal %d\n",
                 program_name, WTERMSIG (status));
        r = WTERMSIG (status) + 128;
      }
    }

    exit (r);
  }

  free (cmd);

  debug ("forked into background (new pid = %d)", getpid ());
}

#else /* WIN32 */

void
run_command (void)
{
  if (!run)
    return;

  NOT_IMPLEMENTED_ON_WINDOWS ("--run");
}

#endif /* WIN32 */
