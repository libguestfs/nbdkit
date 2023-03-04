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

#ifdef WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <fcntl.h>
#include <errno.h>

#include "windows-compat.h"

#undef accept
#undef bind
#undef closesocket
#undef getpeername
#undef listen
#undef getsockopt
#undef recv
#undef setsockopt
#undef socket
#undef send

#define GET_SOCKET_FROM_FD(fd)     \
  SOCKET sk = _get_osfhandle (fd); \
  if (sk == INVALID_SOCKET) {      \
    errno = EBADF;                 \
    return -1;                     \
  }

/* Sockets are non-blocking by default.  Make them blocking.  This
 * introduces a bunch of caveats, see:
 * http://www.sockets.com/winsock.htm#Overview_BlockingNonBlocking
 */
static int
set_blocking (SOCKET sk)
{
  u_long arg = 0;

  if (ioctlsocket (sk, FIONBIO, &arg) < 0) {
    errno = translate_winsock_error ("ioctlsocket", WSAGetLastError ());
    return -1;
  }
  return 0;
}

int
win_accept (int fd, struct sockaddr *addr, socklen_t *len)
{
  SOCKET new_sk;
  GET_SOCKET_FROM_FD (fd);

  new_sk = accept (sk, addr, len);
  if (new_sk == INVALID_SOCKET) {
    errno = translate_winsock_error ("accept", WSAGetLastError ());
    return -1;
  }
  if (set_blocking (new_sk) == -1) return -1;
  return _open_osfhandle ((intptr_t) new_sk, O_RDWR|O_BINARY);
}

int
win_bind (int fd, const struct sockaddr *addr, socklen_t len)
{
  GET_SOCKET_FROM_FD (fd);

  if (bind (sk, addr, len) < 0) {
    errno = translate_winsock_error ("bind", WSAGetLastError ());
    return -1;
  }

  return 0;
}

int
win_closesocket (int fd)
{
  GET_SOCKET_FROM_FD (fd);

  if (closesocket (sk) < 0) {
    errno = translate_winsock_error ("closesocket", WSAGetLastError ());
    return -1;
  }

  return 0;
}

int
win_getpeername (int fd, struct sockaddr *addr, socklen_t *len)
{
  GET_SOCKET_FROM_FD (fd);

  if (getpeername (sk, addr, len) < 0) {
    errno = translate_winsock_error ("getpeername", WSAGetLastError ());
    return -1;
  }

  return 0;
}

int
win_listen (int fd, int backlog)
{
  GET_SOCKET_FROM_FD (fd);

  if (listen (sk, backlog) < 0) {
    errno = translate_winsock_error ("listen", WSAGetLastError ());
    return -1;
  }

  return 0;
}

int
win_getsockopt (int fd, int level, int optname,
                void *optval, socklen_t *optlen)
{
  GET_SOCKET_FROM_FD (fd);

  if (getsockopt (sk, level, optname, optval, optlen) < 0) {
    errno = translate_winsock_error ("getsockopt", WSAGetLastError ());
    return -1;
  }

  return 0;
}

int
win_recv (int fd, void *buf, size_t len, int flags)
{
  int r;
  GET_SOCKET_FROM_FD (fd);

  r = recv (sk, buf, len, flags);
  if (r < 0) {
    errno = translate_winsock_error ("recv", WSAGetLastError ());
    return -1;
  }

  return r;
}

int
win_setsockopt (int fd, int level, int optname,
                const void *optval, socklen_t optlen)
{
  GET_SOCKET_FROM_FD (fd);

  if (setsockopt (sk, level, optname, optval, optlen) < 0) {
    errno = translate_winsock_error ("setsockopt", WSAGetLastError ());
    return -1;
  }

  return 0;
}

int
win_socket (int domain, int type, int protocol)
{
  SOCKET sk;

  sk = WSASocket (domain, type, protocol, NULL, 0, 0);
  if (sk == INVALID_SOCKET) {
    errno = translate_winsock_error ("socket", WSAGetLastError ());
    return -1;
  }

  if (set_blocking (sk) == -1) return -1;
  return _open_osfhandle ((intptr_t) sk, O_RDWR|O_BINARY);
}

int
win_send (int fd, const void *buf, size_t len, int flags)
{
  int r;
  GET_SOCKET_FROM_FD (fd);

  r = send (sk, buf, len, flags);
  if (r < 0) {
    errno = translate_winsock_error ("send", WSAGetLastError ());
    return -1;
  }

  return r;
}

#endif /* WIN32 */
