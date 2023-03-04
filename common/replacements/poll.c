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

/* Replacement for poll for platforms which lack this function. */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>

#ifndef HAVE_POLL

#include "poll.h"

#ifdef WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <errno.h>

/* This is provided by common/utils which hasn't been compiled yet.
 * Programs using the poll replacement will need to link to
 * libutils.la. XXX
 */
extern int translate_winsock_error (const char *fn, int err);

/* Windows doesn't have poll.  It has something called WSAPoll in
 * Winsock, but even Microsoft admit it is broken.  Gnulib contains an
 * elaborate emulation of poll written by Paolo Bonzini, but it's
 * distributed under an incompatible license.  However Winsock has
 * select so we can write a simple (but slow) emulation of poll using
 * select.
 */
int
poll (struct pollfd *fds, int n, int timeout)
{
  int i, r;
  fd_set readfds, writefds;
  struct timeval tv, *tvp;

  /* https://docs.microsoft.com/en-us/windows/win32/winsock/maximum-number-of-sockets-supported-2 */
  if (n >= 64) {
    errno = EINVAL;
    return -1;
  }

  FD_ZERO (&readfds);
  FD_ZERO (&writefds);

  for (i = 0; i < n; ++i) {
    if (fds[i].events & POLLIN)
      FD_SET (fds[i].fd, &readfds);
    if (fds[i].events & POLLOUT)
      FD_SET (fds[i].fd, &writefds);
    fds[i].revents = 0;
  }

  if (timeout >= 0) {
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = timeout % 1000;
    tvp = &tv;
  }
  else
    tvp = NULL;

  /* Windows ignores the nfds parameter of select. */
  r = select (0, &readfds, &writefds, NULL, tvp);
  if (r == -1) {
    errno = translate_winsock_error ("select", WSAGetLastError ());
    return -1;
  }

  r = 0;
  for (i = 0; i < n; ++i) {
    if (FD_ISSET (fds[i].fd, &readfds))
      fds[i].revents |= POLLIN;
    if (FD_ISSET (fds[i].fd, &writefds))
      fds[i].revents |= POLLOUT;
    if (fds[i].revents != 0)
      r++;
  }

  return r;
}

#else /* !WIN32 */
#error "no replacement poll is available on this platform"
#endif

#endif /* !HAVE_POLL */
