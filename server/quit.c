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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "internal.h"
#include "utils.h"

/* Detection of request to exit via signal.  Most places in the code
 * can just poll quit at opportune moments, while sockets.c needs a
 * pipe-to-self through quit_fd in order to break a poll loop without
 * a race.
 */
volatile int quit;

#ifndef WIN32
int quit_fd;
static int write_quit_fd;
#else
HANDLE quit_fd;
#endif

#ifndef WIN32

void
set_up_quit_pipe (void)
{
  int fds[2];

#ifdef HAVE_PIPE2
  if (pipe2 (fds, O_CLOEXEC) < 0) {
    perror ("pipe2");
    exit (EXIT_FAILURE);
  }
#else
  /* This is called early enough that no other thread will be
   * fork()ing while we create this; but we must set CLOEXEC so that
   * the fds don't leak into children.
   */
  if (pipe (fds) < 0) {
    perror ("pipe");
    exit (EXIT_FAILURE);
  }
  if (set_cloexec (fds[0]) == -1 ||
      set_cloexec (fds[1]) == -1) {
    perror ("fcntl");
    exit (EXIT_FAILURE);
  }
#endif
  quit_fd = fds[0];
  write_quit_fd = fds[1];
}

void
close_quit_pipe (void)
{
  close (quit_fd);
  close (write_quit_fd);
}

static void
set_quit (void)
{
  char c = 0;

  quit = 1;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
  write (write_quit_fd, &c, 1);
#pragma GCC diagnostic pop
}

#else /* WIN32 */

/* Pipes don't work well with WaitForMultipleObjectsEx in Windows.  In
 * any case, an Event is a better match with what we are trying to do
 * here.
 */
void
set_up_quit_pipe (void)
{
  quit_fd = CreateEventA (NULL, FALSE, FALSE, NULL);
}

void
close_quit_pipe (void)
{
  CloseHandle (quit_fd);
}

void
set_quit (void)
{
  quit = 1;
  SetEvent (quit_fd);
}

#endif /* WIN32 */

void
handle_quit (int sig)
{
  set_quit ();
}

void
nbdkit_shutdown (void)
{
  set_quit ();
}
