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
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#undef NDEBUG /* Keep test strong even for nbdkit built without assertions */
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <libnbd.h>

#define SOCKET "pause.sock"

static bool command1_completed = false;
static bool command2_completed = false;

static int
callback (void *vp, int *err)
{
  bool *flag = vp;
  *flag = true;
  return 1;
}

int
main (int argc, char *argv[])
{
  struct nbd_handle *nbd;
  int ctrlsock;
  struct sockaddr_un addr;
  char buf[512];
  char c;
  int64_t cookie;
  time_t start_t, t;

  nbd = nbd_create ();
  if (nbd == NULL) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  if (nbd_connect_command (nbd,
                           (char *[]) {
                             "nbdkit", "-s", "--exit-with-parent",
                             "--filter", "pause",
                             "example1", "pause-control=" SOCKET,
                             NULL }) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  /* Connect separately to the pause control socket. */
  ctrlsock = socket (AF_UNIX, SOCK_STREAM, 0);
  if (ctrlsock == -1) {
    perror ("socket");
    exit (EXIT_FAILURE);
  }
  addr.sun_family = AF_UNIX;
  strcpy (addr.sun_path, SOCKET);

  if (connect (ctrlsock, (struct sockaddr *) &addr, sizeof addr) == -1) {
    perror (SOCKET);
    exit (EXIT_FAILURE);
  }

  /* To start with, we should be able to read synchronously normally. */
  if (nbd_pread (nbd, buf, sizeof buf, 0, 0) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  /* Pause the connection. */
  fprintf (stderr, "pausing the connection\n");
  c = 'p';
  if (write (ctrlsock, &c, 1) != 1) {
    perror ("write: ctrlsock: pause");
    exit (EXIT_FAILURE);
  }
  if (read (ctrlsock, &c, 1) != 1) {
    perror ("read: ctrlsock: response to pause");
    exit (EXIT_FAILURE);
  }
  assert (c == 'P');

  /* Issue some asynchronous commands.  These should hang. */
  cookie = nbd_aio_pread (nbd, buf, sizeof buf, 0,
                          (nbd_completion_callback) {
                            .callback = callback,
                            .user_data = &command1_completed,
                          }, 0);
  if (cookie == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }
  cookie = nbd_aio_pread (nbd, buf, sizeof buf, 0,
                          (nbd_completion_callback) {
                            .callback = callback,
                            .user_data = &command2_completed,
                          }, 0);
  if (cookie == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  /* Wait a bit to check they don't complete. */
  time (&start_t);
  while (time (&t) <= start_t + 5)
    nbd_poll (nbd, 1000);
  assert (!command1_completed);
  assert (!command2_completed);

  /* Resume the connection. */
  fprintf (stderr, "resuming the connection\n");
  c = 'r';
  if (write (ctrlsock, &c, 1) != 1) {
    perror ("write: ctrlsock: resume");
    exit (EXIT_FAILURE);
  }
  if (read (ctrlsock, &c, 1) != 1) {
    perror ("read: ctrlsock: response to resume");
    exit (EXIT_FAILURE);
  }
  assert (c == 'R');

  /* Now at least one of the commands should complete. */
  time (&start_t);
  while (!command1_completed && !command2_completed &&
         time (&t) <= start_t + 60)
    nbd_poll (nbd, 1000);
  assert (command1_completed || command2_completed);

  close (ctrlsock);
  nbd_shutdown (nbd, 0);
  nbd_close (nbd);
  exit (EXIT_SUCCESS);
}
