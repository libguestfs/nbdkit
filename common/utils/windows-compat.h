/* nbdkit
 * Copyright (C) 2020 Red Hat Inc.
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

#ifndef NBDKIT_WINDOWS_COMPAT_H
#define NBDKIT_WINDOWS_COMPAT_H

#ifdef WIN32

#include <config.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <errno.h>

/* Windows doesn't have O_CLOEXEC, but it also doesn't have file
 * descriptors that can be inherited across exec.  Similarly for
 * O_NOCTTY.
 */
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif

/* AI_ADDRCONFIG is not available on Windows.  It enables a rather
 * obscure feature of getaddrinfo to do with IPv6.
 */
#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG 0
#endif

/* Windows <errno.h> lacks certain errnos, so replace them here as
 * best we can.
 */
#ifndef EBADMSG
#define EBADMSG EPROTO
#endif
#ifndef ESHUTDOWN
#define ESHUTDOWN ECONNABORTED
#endif

/* This generated function translates Winsock errors into errno codes. */
extern int translate_winsock_error (const char *fn, int err);

/* Add wrappers around the Winsock syscalls that nbdkit uses. */
extern int win_accept (int fd, struct sockaddr *addr, socklen_t *len);
extern int win_bind (int fd, const struct sockaddr *addr, socklen_t len);
extern int win_closesocket (int fd);
extern int win_getpeername (int fd, struct sockaddr *addr, socklen_t *len);
extern int win_listen (int fd, int backlog);
extern int win_getsockopt (int fd, int level, int optname,
                           void *optval, socklen_t *optlen);
extern int win_recv (int fd, void *buf, size_t len, int flags);
extern int win_setsockopt (int fd, int level, int optname,
                           const void *optval, socklen_t optlen);
extern int win_socket (int domain, int type, int protocol);
extern int win_send (int fd, const void *buf, size_t len, int flags);

#define accept win_accept
#define bind win_bind
#define closesocket win_closesocket
#define getpeername win_getpeername
#define listen win_listen
#define getsockopt win_getsockopt
#define recv win_recv
#define setsockopt win_setsockopt
#define socket win_socket
#define send win_send

/* Windows has strange names for these functions. */
#define dup _dup
#define dup2 _dup2

/* setenv replacement. */
#define setenv(k, v, replace) _putenv_s ((k), (v));

/* Unfortunately quite commonly used at the moment.  Make it a common
 * macro so we can easily find places which need porting.
 *
 * Note: Don't use this for things which can never work on Windows
 * (eg. Unix socket support).  Those should just give regular errors.
 */
#define NOT_IMPLEMENTED_ON_WINDOWS(feature)                             \
  do {                                                                  \
    fprintf (stderr, "nbdkit: %s is not implemented for Windows.\n", feature); \
    fprintf (stderr, "You can help by contributing to the Windows port, see\n"); \
    fprintf (stderr, "nbdkit README in the source for how to contribute.\n"); \
    exit (EXIT_FAILURE);                                                \
  } while (0)

#else /* !WIN32 */

/* Windows doesn't have a generic function for closing anything,
 * instead you have to call closesocket on a SOCKET object.  We would
 * like to #define close to point to the Windows alternative above,
 * but that's not possible because it breaks things like
 * backend->close.  So instead the server code must call closesocket()
 * on anything that might be a socket.
 */
#define closesocket close

#endif /* !WIN32 */

#endif /* NBDKIT_WINDOWS_COMPAT_H */
