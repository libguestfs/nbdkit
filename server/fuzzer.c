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
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "array-size.h"

#include "internal.h"

#ifndef ENABLE_LIBFUZZER
#error "This file should only be compiled when libFuzzer is enabled"
#endif

/* When we're compiled with --enable-libfuzzer, the normal main()
 * function is renamed to fuzzer_main.  We call it with particular
 * parameters to make it use the memory plugin.
 */
extern int fuzzer_main (int argc, char *argv[]);

static void server (int sock);
static void client (const uint8_t *data, size_t size, int sock);

/* This is the entry point called by libFuzzer. */
int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  pid_t pid;
  int sv[2], r, status;

  /* Create a connected socket. */
  if (socketpair (AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0, sv) == -1) {
    perror ("socketpair");
    exit (EXIT_FAILURE);
  }

  /* Fork: The parent will be the nbdkit process (server).  The child
   * will be the phony NBD client.
   */
  pid = fork ();
  if (pid == -1) {
    perror ("fork");
    exit (EXIT_FAILURE);
  }

  if (pid > 0) {
    /* Parent: nbdkit server. */
    close (sv[1]);

    server (sv[0]);

    close (sv[0]);

  again:
    r = wait (&status);
    if (r == -1) {
      if (errno == EINTR)
        goto again;
      perror ("wait");
      exit (EXIT_FAILURE);
    }
    if (!WIFEXITED (status) || WEXITSTATUS (status) != 0)
      fprintf (stderr, "bad exit status %d\n", status);

    return 0;
  }

  /* Child: phony NBD client. */
  close (sv[0]);

  client (data, size, sv[1]);

  close (sv[1]);

  _exit (EXIT_SUCCESS);
}

static void
server (int sock)
{
  char *argv[] = {
    "nbdkit",
    "-s",         /* take input from stdin/stdout */
    "--log=null", /* discard error messages */
    "plugins/memory/.libs/nbdkit-memory-plugin." SOEXT, "1M",
    NULL
  };
  const int argc = ARRAY_SIZE (argv) - 1;
  int saved_stdin, saved_stdout;

  /* Make the socket appear as stdin and stdout of the process, saving
   * the existing stdin/stdout.
   */
  saved_stdin = dup (0);
  saved_stdout = dup (1);
  dup2 (sock, 0);
  dup2 (sock, 1);

  /* Call nbdkit's normal main() function. */
  fuzzer_main (argc, argv);

  /* Restore stdin/stdout. */
  dup2 (saved_stdin, 0);
  dup2 (saved_stdout, 1);
  close (saved_stdin);
  close (saved_stdout);
}

static void
client (const uint8_t *data, size_t size, int sock)
{
  struct pollfd pfds[1];
  char rbuf[512];
  ssize_t r;

  if (size == 0)
    shutdown (sock, SHUT_WR);

  for (;;) {
    pfds[0].fd = sock;
    pfds[0].events = POLLIN;
    if (size > 0) pfds[0].events |= POLLOUT;
    pfds[0].revents = 0;

    if (poll (pfds, 1, -1) == -1) {
      if (errno == EINTR)
        continue;
      perror ("poll");
      /* This is not an error. */
      return;
    }

    /* We can read from the server socket.  Just throw away anything sent. */
    if ((pfds[0].revents & POLLIN) != 0) {
      r = read (sock, rbuf, sizeof rbuf);
      if (r == -1 && errno != EINTR) {
        //perror ("read");
        return;
      }
      else if (r == 0)          /* end of input from the server */
        return;
    }

    /* We can write to the server socket. */
    if ((pfds[0].revents & POLLOUT) != 0) {
      if (size > 0) {
        r = write (sock, data, size);
        if (r == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
          perror ("write");
          return;
        }
        else if (r > 0) {
          data += r;
          size -= r;

          if (size == 0)
            shutdown (sock, SHUT_WR);
        }
      }
    }
  } /* for (;;) */
}
