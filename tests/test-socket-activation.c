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

/* Test socket activation.
 *
 * We cannot use the test framework for this since the framework
 * always uses the -U flag which is incompatible with socket
 * activation.  Unfortunately this does mean we duplicate some code
 * from the test framework.
 *
 * It's *almost* possible to test this from a shell script
 * (cf. test-ip.sh) but as far as I can tell setting LISTEN_PID
 * correctly is impossible from shell.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include "byte-swapping.h"
#include "nbd-protocol.h"

#ifndef SOCK_CLOEXEC
/* For this file, we don't care if fds are marked cloexec; leaking is okay.  */
#define SOCK_CLOEXEC 0
#endif

#define FIRST_SOCKET_ACTIVATION_FD 3

#define NBDKIT_START_TIMEOUT 30 /* seconds */

/* Declare program_name. */
#if HAVE_DECL_PROGRAM_INVOCATION_SHORT_NAME == 1
#include <errno.h>
#define program_name program_invocation_short_name
#else
#define program_name "nbdkit"
#endif

static char tmpdir[] =   "/tmp/nbdkitXXXXXX";
static char sockpath[] = "/tmp/nbdkitXXXXXX/sock";
static char pidpath[] =  "/tmp/nbdkitXXXXXX/pid";

static pid_t pid = 0;

static void
cleanup (void)
{
  if (pid > 0)
    kill (pid, SIGTERM);

  unlink (pidpath);
  unlink (sockpath);
  rmdir (tmpdir);
}

int
main (int argc, char *argv[])
{
  int sock;
  struct sockaddr_un addr;
  char pid_str[16];
  size_t i, len;
  uint64_t magic;

  if (mkdtemp (tmpdir) == NULL) {
    perror ("mkdtemp");
    exit (EXIT_FAILURE);
  }
  len = strlen (tmpdir);
  memcpy (sockpath, tmpdir, len);
  memcpy (pidpath, tmpdir, len);

  atexit (cleanup);

  /* Open the listening socket which will be passed into nbdkit. */
  sock = socket (AF_UNIX, SOCK_STREAM /* NB do not use SOCK_CLOEXEC */, 0);
  if (sock == -1) {
    perror ("socket");
    exit (EXIT_FAILURE);
  }

  addr.sun_family = AF_UNIX;
  len = strlen (sockpath);
  memcpy (addr.sun_path, sockpath, len+1 /* trailing \0 */);

  if (bind (sock, (struct sockaddr *) &addr, sizeof addr) == -1) {
    perror (sockpath);
    exit (EXIT_FAILURE);
  }

  if (listen (sock, 1) == -1) {
    perror ("listen");
    exit (EXIT_FAILURE);
  }

  if (sock != FIRST_SOCKET_ACTIVATION_FD) {
    if (dup2 (sock, FIRST_SOCKET_ACTIVATION_FD) == -1) {
      perror ("dup2");
      exit (EXIT_FAILURE);
    }
    close (sock);
  }

  /* Run nbdkit. */
  pid = fork ();
  if (pid == -1) {
    perror ("fork");
    exit (EXIT_FAILURE);
  }
  if (pid == 0) {
    /* Run nbdkit in the child. */
    setenv ("LISTEN_FDS", "1", 1);
    snprintf (pid_str, sizeof pid_str, "%d", (int) getpid ());
    setenv ("LISTEN_PID", pid_str, 1);

    execlp ("nbdkit",
            "nbdkit",
            "-P", pidpath,
            "-o",
            "-v",
            "example1", NULL);
    perror ("exec: nbdkit");
    _exit (EXIT_FAILURE);
  }

  /* We don't need the listening socket now. */
  close (sock);

  /* Wait for the pidfile to turn up, which indicates that nbdkit has
   * started up successfully and is ready to serve requests.  However
   * if 'pid' exits in this time it indicates a failure to start up.
   * Also there is a timeout in case nbdkit hangs.
   */
  for (i = 0; i < NBDKIT_START_TIMEOUT; ++i) {
    if (waitpid (pid, NULL, WNOHANG) == pid)
      goto early_exit;

    if (kill (pid, 0) == -1) {
      if (errno == ESRCH) {
      early_exit:
        fprintf (stderr,
                 "%s FAILED: nbdkit exited before starting to serve files\n",
                 program_name);
        pid = 0;
        exit (EXIT_FAILURE);
      }
      perror ("kill");
    }

    if (access (pidpath, F_OK) == 0)
      break;

    sleep (1);
  }

  /* Now nbdkit is supposed to be listening on the Unix domain socket
   * (which it got via the listening socket that we passed down to it,
   * not from the path), so we should be able to connect to the Unix
   * domain socket by its path and receive an NBD magic string.
   */
  sock = socket (AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
  if (sock == -1) {
    perror ("socket");
    exit (EXIT_FAILURE);
  }

  /* Reuse addr which was set up above. */
  if (connect (sock, (struct sockaddr *) &addr, sizeof addr) == -1) {
    perror (sockpath);
    exit (EXIT_FAILURE);
  }

  if (read (sock, &magic, sizeof magic) != sizeof magic) {
    perror ("read");
    exit (EXIT_FAILURE);
  }

  if (be64toh (magic) != NBD_MAGIC) {
    fprintf (stderr, "%s FAILED: did not read magic string from server\n",
             program_name);
    exit (EXIT_FAILURE);
  }

  close (sock);

  /* Test succeeded. */
  exit (EXIT_SUCCESS);
}
