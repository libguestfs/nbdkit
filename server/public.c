/* nbdkit
 * Copyright (C) 2013-2023 Red Hat Inc.
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

/* This file contains the public utility APIs to be exported by nbdkit
 * for use by filters and plugins, declared in nbdkit-common.h.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_SYS_UCRED_H
#include <sys/ucred.h>
#endif

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif

#ifdef WIN32
/* For nanosleep on Windows. */
#include <pthread_time.h>
#endif

#include "array-size.h"
#include "ascii-ctype.h"
#include "ascii-string.h"
#include "get_current_dir_name.h"
#include "getline.h"
#include "poll.h"
#include "realpath.h"
#include "strndup.h"

#include "internal.h"

#ifndef WIN32

NBDKIT_DLL_PUBLIC char *
nbdkit_absolute_path (const char *path)
{
  CLEANUP_FREE char *pwd = NULL;
  char *ret;

  if (path == NULL || *path == '\0') {
    nbdkit_error ("cannot convert null or empty path to an absolute path");
    return NULL;
  }

  if (*path == '/') {
    ret = strdup (path);
    if (!ret) {
      nbdkit_error ("strdup: %m");
      return NULL;
    }
    return ret;
  }

  pwd = get_current_dir_name ();
  if (pwd == NULL) {
    nbdkit_error ("get_current_dir_name: %m");
    return NULL;
  }

  if (asprintf (&ret, "%s" DIR_SEPARATOR_STR "%s", pwd, path) == -1) {
    nbdkit_error ("asprintf: %m");
    return NULL;
  }

  return ret;
}

#else /* WIN32 */

/* On Windows realpath() is replaced by GetFullPathName which doesn't
 * bother to check if the final path exists.  Therefore we can simply
 * replace nbdkit_absolute_path with nbdkit_realpath and everything
 * should work the same.
 */
NBDKIT_DLL_PUBLIC char *
nbdkit_absolute_path (const char *path)
{
  return nbdkit_realpath (path);
}

#endif /* WIN32 */

NBDKIT_DLL_PUBLIC char *
nbdkit_realpath (const char *path)
{
  char *ret;

  if (path == NULL || *path == '\0') {
    nbdkit_error ("cannot resolve a null or empty path");
    return NULL;
  }

  ret = realpath (path, NULL);
  if (ret == NULL) {
    nbdkit_error ("realpath: %s: %m", path);
    return NULL;
  }

  return ret;
}

/* Common code for parsing integers. */
#define PARSE_COMMON_TAIL                                               \
  if (errno != 0) {                                                     \
    nbdkit_error ("%s: could not parse number: \"%s\": %m",             \
                  what, str);                                           \
    return -1;                                                          \
  }                                                                     \
  if (end == str) {                                                     \
    nbdkit_error ("%s: empty string where we expected a number",        \
                  what);                                                \
    return -1;                                                          \
  }                                                                     \
  if (*end) {                                                           \
    nbdkit_error ("%s: could not parse number: \"%s\": trailing garbage", \
                  what, str);                                           \
    return -1;                                                          \
  }                                                                     \
                                                                        \
  if (rp)                                                               \
    *rp = r;                                                            \
  return 0

/* Functions for parsing signed integers. */
NBDKIT_DLL_PUBLIC int
nbdkit_parse_int (const char *what, const char *str, int *rp)
{
  long r;
  char *end;

  errno = 0;
  r = strtol (str, &end, 0);
#if INT_MAX != LONG_MAX
  if (r < INT_MIN || r > INT_MAX)
    errno = ERANGE;
#endif
  PARSE_COMMON_TAIL;
}

NBDKIT_DLL_PUBLIC int
nbdkit_parse_int8_t (const char *what, const char *str, int8_t *rp)
{
  long r;
  char *end;

  errno = 0;
  r = strtol (str, &end, 0);
  if (r < INT8_MIN || r > INT8_MAX)
    errno = ERANGE;
  PARSE_COMMON_TAIL;
}

NBDKIT_DLL_PUBLIC int
nbdkit_parse_int16_t (const char *what, const char *str, int16_t *rp)
{
  long r;
  char *end;

  errno = 0;
  r = strtol (str, &end, 0);
  if (r < INT16_MIN || r > INT16_MAX)
    errno = ERANGE;
  PARSE_COMMON_TAIL;
}

NBDKIT_DLL_PUBLIC int
nbdkit_parse_int32_t (const char *what, const char *str, int32_t *rp)
{
  long r;
  char *end;

  errno = 0;
  r = strtol (str, &end, 0);
#if INT32_MAX != LONG_MAX
  if (r < INT32_MIN || r > INT32_MAX)
    errno = ERANGE;
#endif
  PARSE_COMMON_TAIL;
}

NBDKIT_DLL_PUBLIC int
nbdkit_parse_int64_t (const char *what, const char *str, int64_t *rp)
{
  long long r;
  char *end;

  errno = 0;
  r = strtoll (str, &end, 0);
#if INT64_MAX != LONGLONG_MAX
  if (r < INT64_MIN || r > INT64_MAX)
    errno = ERANGE;
#endif
  PARSE_COMMON_TAIL;
}

/* Functions for parsing unsigned integers. */

/* strtou* functions have surprising behaviour if the first character
 * (after whitespace) is '-', so reject this early.
 */
#define PARSE_ERROR_IF_NEGATIVE                                         \
  do {                                                                  \
    while (ascii_isspace (*str))                                        \
      str++;                                                            \
    if (*str == '-') {                                                  \
      nbdkit_error ("%s: negative numbers are not allowed", what);      \
      return -1;                                                        \
    }                                                                   \
  } while (0)

NBDKIT_DLL_PUBLIC int
nbdkit_parse_unsigned (const char *what, const char *str, unsigned *rp)
{
  unsigned long r;
  char *end;

  PARSE_ERROR_IF_NEGATIVE;
  errno = 0;
  r = strtoul (str, &end, 0);
#if UINT_MAX != ULONG_MAX
  if (r > UINT_MAX)
    errno = ERANGE;
#endif
  PARSE_COMMON_TAIL;
}

NBDKIT_DLL_PUBLIC int
nbdkit_parse_uint8_t (const char *what, const char *str, uint8_t *rp)
{
  unsigned long r;
  char *end;

  PARSE_ERROR_IF_NEGATIVE;
  errno = 0;
  r = strtoul (str, &end, 0);
  if (r > UINT8_MAX)
    errno = ERANGE;
  PARSE_COMMON_TAIL;
}

NBDKIT_DLL_PUBLIC int
nbdkit_parse_uint16_t (const char *what, const char *str, uint16_t *rp)
{
  unsigned long r;
  char *end;

  PARSE_ERROR_IF_NEGATIVE;
  errno = 0;
  r = strtoul (str, &end, 0);
  if (r > UINT16_MAX)
    errno = ERANGE;
  PARSE_COMMON_TAIL;
}

NBDKIT_DLL_PUBLIC int
nbdkit_parse_uint32_t (const char *what, const char *str, uint32_t *rp)
{
  unsigned long r;
  char *end;

  PARSE_ERROR_IF_NEGATIVE;
  errno = 0;
  r = strtoul (str, &end, 0);
#if UINT32_MAX != ULONG_MAX
  if (r > UINT32_MAX)
    errno = ERANGE;
#endif
  PARSE_COMMON_TAIL;
}

NBDKIT_DLL_PUBLIC int
nbdkit_parse_uint64_t (const char *what, const char *str, uint64_t *rp)
{
  unsigned long long r;
  char *end;

  PARSE_ERROR_IF_NEGATIVE;
  errno = 0;
  r = strtoull (str, &end, 0);
#if UINT64_MAX != ULONGLONG_MAX
  if (r > UINT64_MAX)
    errno = ERANGE;
#endif
  PARSE_COMMON_TAIL;
}

/* Parse a string as a size with possible scaling suffix, or return -1
 * after reporting the error.
 */
NBDKIT_DLL_PUBLIC int64_t
nbdkit_parse_size (const char *str)
{
  int64_t size;
  char *end;
  uint64_t scale = 1;

  /* Disk sizes cannot usefully exceed off_t (which is signed) and
   * cannot be negative.  */
  /* XXX Should we also parse things like '1.5M'? */
  /* XXX Should we allow hex? If so, hex cannot use scaling suffixes,
   * because some of them are valid hex digits */
  errno = 0;
  size = strtoimax (str, &end, 10);
  if (str == end) {
    nbdkit_error ("could not parse size string (%s)", str);
    return -1;
  }
  if (size < 0) {
    nbdkit_error ("size cannot be negative (%s)", str);
    return -1;
  }
  if (errno) {
    nbdkit_error ("size (%s) exceeds maximum value", str);
    return -1;
  }

  switch (*end) {
    /* No suffix */
  case '\0':
    end--; /* Safe, since we already filtered out empty string */
    break;

    /* Powers of 1024 */
  case 'e': case 'E':
    scale *= 1024;
    /* fallthru */
  case 'p': case 'P':
    scale *= 1024;
    /* fallthru */
  case 't': case 'T':
    scale *= 1024;
    /* fallthru */
  case 'g': case 'G':
    scale *= 1024;
    /* fallthru */
  case 'm': case 'M':
    scale *= 1024;
    /* fallthru */
  case 'k': case 'K':
    scale *= 1024;
    /* fallthru */
  case 'b': case 'B':
    break;

    /* "sectors", ie. units of 512 bytes, even if that's not the real
     * sector size */
  case 's': case 'S':
    scale = 512;
    break;

  default:
    nbdkit_error ("could not parse size: unknown suffix '%s'", end);
    return -1;
  }

  /* XXX Maybe we should support 'MiB' as a synonym for 'M'; and 'MB'
   * for powers of 1000, for similarity to GNU tools. But for now,
   * anything beyond 'M' is dropped.  */
  if (end[1]) {
    nbdkit_error ("could not parse size: unknown suffix '%s'", end);
    return -1;
  }

  if (INT64_MAX / scale < size) {
    nbdkit_error ("overflow computing size (%s)", str);
    return -1;
  }

  return size * scale;
}

/* Parse a string as a boolean, or return -1 after reporting the error.
 */
NBDKIT_DLL_PUBLIC int
nbdkit_parse_bool (const char *str)
{
  if (!strcmp (str, "1") ||
      !ascii_strcasecmp (str, "true") ||
      !ascii_strcasecmp (str, "t") ||
      !ascii_strcasecmp (str, "yes") ||
      !ascii_strcasecmp (str, "y") ||
      !ascii_strcasecmp (str, "on"))
    return 1;

  if (!strcmp (str, "0") ||
      !ascii_strcasecmp (str, "false") ||
      !ascii_strcasecmp (str, "f") ||
      !ascii_strcasecmp (str, "no") ||
      !ascii_strcasecmp (str, "n") ||
      !ascii_strcasecmp (str, "off"))
    return 0;

  nbdkit_error ("could not decipher boolean (%s)", str);
  return -1;
}

/* Return true if it is safe to read from stdin during configuration. */
NBDKIT_DLL_PUBLIC int
nbdkit_stdio_safe (void)
{
  return !listen_stdin && !configured;
}

/* Read a password from configuration value. */
static int read_password_interactive (char **password);
static int read_password_from_fd (const char *what, int fd, char **password);

NBDKIT_DLL_PUBLIC int
nbdkit_read_password (const char *value, char **password)
{
  *password = NULL;

  /* Read from stdin interactively. */
  if (strcmp (value, "-") == 0) {
    if (read_password_interactive (password) == -1)
      return -1;
  }

  /* Read from numbered file descriptor. */
  else if (value[0] == '-') {
#ifndef WIN32
    int fd;

    if (nbdkit_parse_int ("password file descriptor", &value[1], &fd) == -1)
      return -1;
    if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO) {
      nbdkit_error ("cannot use password -FD for stdin/stdout/stderr");
      return -1;
    }
    if (read_password_from_fd (&value[1], fd, password) == -1)
      return -1;

#else /* WIN32 */
    /* As far as I know this will never be possible on Windows, so
     * it's a simple error.
     */
    nbdkit_error ("not possible to read passwords from file descriptors "
                  "under Windows");
    return -1;
#endif /* WIN32 */
  }

  /* Read password from a file. */
  else if (value[0] == '+') {
    int fd;

    fd = open (&value[1], O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
      nbdkit_error ("open %s: %m", &value[1]);
      return -1;
    }
    if (read_password_from_fd (&value[1], fd, password) == -1)
      return -1;
  }

  /* Parameter is the password. */
  else {
    *password = strdup (value);
    if (*password == NULL) {
      nbdkit_error ("strdup: %m");
      return -1;
    }
  }

  return 0;
}

#ifndef WIN32

typedef struct termios echo_mode;

static void
echo_off (echo_mode *old_mode)
{
  struct termios temp;

  tcgetattr (STDIN_FILENO, old_mode);
  temp = *old_mode;
  temp.c_lflag &= ~ECHO;
  tcsetattr (STDIN_FILENO, TCSAFLUSH, &temp);
}

static void
echo_restore (const echo_mode *old_mode)
{
  tcsetattr (STDIN_FILENO, TCSAFLUSH, old_mode);
}

#else /* WIN32 */

/* Windows implementation of tty echo off based on this:
 * https://stackoverflow.com/a/1455007
 */
typedef DWORD echo_mode;

static void
echo_off (echo_mode *old_mode)
{
  HANDLE h_stdin;
  DWORD mode;

  h_stdin = GetStdHandle (STD_INPUT_HANDLE);
  GetConsoleMode (h_stdin, old_mode);
  mode = *old_mode;
  mode &= ~ENABLE_ECHO_INPUT;
  SetConsoleMode (h_stdin, mode);
}

static void
echo_restore (const echo_mode *old_mode)
{
  HANDLE h_stdin;

  h_stdin = GetStdHandle (STD_INPUT_HANDLE);
  SetConsoleMode (h_stdin, *old_mode);
}

#endif /* WIN32 */

static int
read_password_interactive (char **password)
{
  int err;
  echo_mode orig;
  ssize_t r;
  size_t n;

  if (!nbdkit_stdio_safe ()) {
    nbdkit_error ("stdin is not available for reading password");
    return -1;
  }

  if (!isatty (STDIN_FILENO)) {
    nbdkit_error ("stdin is not a tty, cannot read password interactively");
    return -1;
  }

  printf ("password: ");

  /* Set no echo. */
  echo_off (&orig);

  /* To distinguish between error and EOF we have to check errno.
   * getline can return -1 and errno = 0 which means we got end of
   * file, which is simply a zero length password.
   */
  errno = 0;
  r = getline (password, &n, stdin);
  err = errno;

  /* Restore echo. */
  echo_restore (&orig);

  /* Complete the printf above. */
  printf ("\n");

  if (r == -1) {
    if (err == 0) {             /* EOF, not an error. */
      free (*password);         /* State of linebuf is undefined. */
      *password = strdup ("");
      if (*password == NULL) {
        nbdkit_error ("strdup: %m");
        return -1;
      }
    }
    else {
      errno = err;
      nbdkit_error ("could not read password from stdin: %m");
      return -1;
    }
  }

  if (*password && r > 0 && (*password)[r-1] == '\n')
    (*password)[r-1] = '\0';

  return 0;
}

static int
read_password_from_fd (const char *what, int fd, char **password)
{
  FILE *fp;
  size_t n;
  ssize_t r;
  int err;

  fp = fdopen (fd, "r");
  if (fp == NULL) {
    nbdkit_error ("fdopen %s: %m", what);
    close (fd);
    return -1;
  }

  /* To distinguish between error and EOF we have to check errno.
   * getline can return -1 and errno = 0 which means we got end of
   * file, which is simply a zero length password.
   */
  errno = 0;
  r = getline (password, &n, fp);
  err = errno;

  fclose (fp);

  if (r == -1) {
    if (err == 0) {             /* EOF, not an error. */
      free (*password);         /* State of linebuf is undefined. */
      *password = strdup ("");
      if (*password == NULL) {
        nbdkit_error ("strdup: %m");
        return -1;
      }
    }
    else {
      errno = err;
      nbdkit_error ("could not read password from %s: %m", what);
      return -1;
    }
  }

  if (*password && r > 0 && (*password)[r-1] == '\n')
    (*password)[r-1] = '\0';

  return 0;
}

NBDKIT_DLL_PUBLIC int
nbdkit_nanosleep (unsigned sec, unsigned nsec)
{
  struct timespec ts;

  if (sec >= INT_MAX - nsec / 1000000000) {
    nbdkit_error ("sleep request is too long");
    errno = EINVAL;
    return -1;
  }
  ts.tv_sec = sec + nsec / 1000000000;
  ts.tv_nsec = nsec % 1000000000;

#if defined HAVE_PPOLL && defined POLLRDHUP
  /* End the sleep early if any of these happen:
   * - nbdkit has received a signal to shut down the server
   * - the current connection is multi-threaded and another thread detects
   *   NBD_CMD_DISC or a problem with the connection
   * - the input socket detects POLLRDHUP/POLLHUP/POLLERR
   * - the input socket is invalid (POLLNVAL, probably closed by
   *   another thread)
   */
  struct connection *conn = threadlocal_get_conn ();
  struct pollfd fds[] = {
    [0].fd = quit_fd,
    [0].events = POLLIN,
    [1].fd = conn ? conn->status_pipe[0] : -1,
    [1].events = POLLIN,
    [2].fd = conn ? conn->sockin : -1,
    [2].events = POLLRDHUP,
  };
  sigset_t all;

  /* Block all signals to this thread during the poll, so we don't
   * have to worry about EINTR
   */
  if (sigfillset (&all))
    abort ();
  switch (ppoll (fds, ARRAY_SIZE (fds), &ts, &all)) {
  case -1:
    assert (errno != EINTR);
    nbdkit_error ("poll: %m");
    return -1;
  case 0:
    return 0;
  }

  /* We don't have to read the pipe-to-self; if poll returned an
   * event, we know the connection should be shutting down.
   */
  bool has_quit = quit;
  assert (has_quit ||
          (conn && conn->nworkers > 0 &&
           connection_get_status () < STATUS_SHUTDOWN) ||
          (conn && (fds[2].revents & (POLLRDHUP | POLLHUP | POLLERR |
                                      POLLNVAL))));
  if (has_quit)
    nbdkit_error ("aborting sleep because of server shut down");
  else
    nbdkit_error ("aborting sleep because of connection close or error");
  errno = ESHUTDOWN;
  return -1;

#else
  /* The fallback path simply calls ordinary nanosleep, and will
   * cause long delays on server shutdown.
   *
   * If however you want to port this to your platform, then
   * porting ideas, in order of preference:
   * - POSIX requires pselect; it's a bit clunkier to set up than poll,
   *   but the same ability to atomically mask all signals and operate
   *   on struct timespec makes it similar to the preferred ppoll interface
   * - calculate an end time target, then use poll in a loop on EINTR with
   *   a recalculation of the timeout to still reach the end time (masking
   *   signals in that case is not safe, as it is a non-atomic race)
   */
  int r;

  r = nanosleep (&ts, NULL);
  if (r == -1 && errno != EINTR && errno != EAGAIN) {
    nbdkit_error ("nanosleep: %m");
    return -1;
  }
  return 0;
#endif
}

/* This function will be deprecated for API V3 users.  The preferred
 * approach will be to get the exportname from .open().
 */
NBDKIT_DLL_PUBLIC const char *
nbdkit_export_name (void)
{
  struct context *c = threadlocal_get_context ();

  if (!c || !c->conn) {
    nbdkit_error ("no connection in this thread");
    return NULL;
  }

  return c->conn->exportname;
}

/* This function will be deprecated for API V3 users.  The preferred
 * approach will be to get the tls mode from .open().
 */
NBDKIT_DLL_PUBLIC int
nbdkit_is_tls (void)
{
  struct context *c = threadlocal_get_context ();

  if (!c) {
    nbdkit_error ("no connection in this thread");
    return -1;
  }

  if (!c->conn) {
    /* If a filter opened this backend outside of a client connection,
     * then we can only claim tls when the command line required it.
     */
    return tls == 2;
  }

  return c->conn->using_tls;
}

NBDKIT_DLL_PUBLIC int
nbdkit_peer_name (struct sockaddr *addr, socklen_t *addrlen)
{
  struct connection *conn = threadlocal_get_conn ();
  int s;

  if (!conn) {
    nbdkit_error ("no connection in this thread");
    return -1;
  }

  s = conn->sockin;
  if (s == -1) {
    nbdkit_error ("socket not open");
    return -1;
  }

  if (getpeername (s, addr, addrlen) == -1) {
    nbdkit_error ("peername: %m");
    return -1;
  }

  return 0;
}

#if defined (SO_PEERCRED) && \
  (defined (HAVE_STRUCT_UCRED_UID) || defined (HAVE_STRUCT_SOCKPEERCRED_UID))

#define GET_PEERCRED_DEFINED 1

static int
get_peercred (int s, int64_t *pid, int64_t *uid, int64_t *gid)
{
#if HAVE_STRUCT_UCRED_UID
  struct ucred cred;
#elif HAVE_STRUCT_SOCKPEERCRED_UID
  /* The struct has a different name on OpenBSD, but the same members. */
  struct sockpeercred cred;
#endif
  socklen_t n = sizeof cred;

  if (getsockopt (s, SOL_SOCKET, SO_PEERCRED, &cred, &n) == -1) {
    nbdkit_error ("getsockopt: SO_PEERCRED: %m");
    return -1;
  }

  if (pid && cred.pid >= 1) {
#if SIZEOF_PID_T >= 8
    if (cred.pid > INT64_MAX)
      nbdkit_error ("pid out of range: cannot be mapped to int64_t");
    else
#endif
      *pid = cred.pid;
  }
  if (uid && cred.uid >= 0) {
#if SIZEOF_UID_T >= 8
    if (cred.uid > INT64_MAX)
      nbdkit_error ("uid out of range: cannot be mapped to int64_t");
    else
#endif
      *uid = cred.uid;
  }
  if (gid && cred.gid >= 0) {
#if SIZEOF_GID_T >= 8
    if (cred.gid > INT64_MAX)
      nbdkit_error ("gid out of range: cannot be mapped to int64_t");
    else
#endif
      *gid = cred.gid;
  }

  return 0;
}

#endif /* SO_PEERCRED */

#ifdef LOCAL_PEERCRED

#define GET_PEERCRED_DEFINED 1

/* FreeBSD supports LOCAL_PEERCRED and struct xucred. */
static int
get_peercred (int s, int64_t *pid, int64_t *uid, int64_t *gid)
{
  struct xucred xucred;
  socklen_t n = sizeof xucred;

  if (getsockopt (s, 0, LOCAL_PEERCRED, &xucred, &n) == -1) {
    nbdkit_error ("getsockopt: LOCAL_PEERCRED: %m");
    return -1;
  }

  if (xucred.cr_version != XUCRED_VERSION) {
    nbdkit_error ("getsockopt: LOCAL_PEERCRED: "
                  "struct xucred version (%u) "
                  "did not match expected version (%u)",
                  xucred.cr_version, XUCRED_VERSION);
    return -1;
  }

  if (n != sizeof xucred) {
    nbdkit_error ("getsockopt: LOCAL_PEERCRED: did not return full struct");
    return -1;
  }

  if (pid)
    nbdkit_error ("nbdkit_peer_pid is not supported on this platform");
  if (uid && xucred.cr_uid >= 0) {
#if SIZEOF_UID_T >= 8
    if (xucred.cr_uid <= INT64_MAX)
#endif
      *uid = xucred.cr_uid;
#if SIZEOF_UID_T >= 8
    else
      nbdkit_error ("uid out of range: cannot be mapped to int64_t");
#endif
  }
  if (gid && xucred.cr_ngroups > 0) {
#if SIZEOF_GID_T >= 8
    if (xucred.cr_gid <= INT64_MAX)
#endif
      *gid = xucred.cr_gid;
#if SIZEOF_GID_T >= 8
    else
      nbdkit_error ("gid out of range: cannot be mapped to int64_t");
#endif
  }

  return 0;
}

#endif /* LOCAL_PEERCRED */

#ifndef GET_PEERCRED_DEFINED

static int
get_peercred (int s, int64_t *pid, int64_t *uid, int64_t *gid)
{
  nbdkit_error ("nbdkit_peer_pid, nbdkit_peer_uid and nbdkit_peer_gid "
                "are not supported on this platform");
  return -1;
}

#endif

static int
get_peercred_common (int64_t *pid, int64_t *uid, int64_t *gid)
{
  struct connection *conn = threadlocal_get_conn ();
  int s;

  if (pid) *pid = -1;
  if (uid) *uid = -1;
  if (gid) *gid = -1;

  if (!conn) {
    nbdkit_error ("no connection in this thread");
    return -1;
  }

  s = conn->sockin;
  if (s == -1) {
    nbdkit_error ("socket not open");
    return -1;
  }

  return get_peercred (s, pid, uid, gid);
}

NBDKIT_DLL_PUBLIC int64_t
nbdkit_peer_pid ()
{
  int64_t pid;

  if (get_peercred_common (&pid, NULL, NULL) == -1)
    return -1;

  return pid;
}

NBDKIT_DLL_PUBLIC int64_t
nbdkit_peer_uid ()
{
  int64_t uid;

  if (get_peercred_common (NULL, &uid, NULL) == -1)
    return -1;

  return uid;
}

NBDKIT_DLL_PUBLIC int64_t
nbdkit_peer_gid ()
{
  int64_t gid;

  if (get_peercred_common (NULL, NULL, &gid) == -1)
    return -1;

  return gid;
}

/* Functions for manipulating intern'd strings. */

static string_vector global_interns;

void
free_interns (void)
{
  struct connection *conn = threadlocal_get_conn ();
  string_vector *list = conn ? &conn->interns : &global_interns;

  string_vector_empty (list);
}

static const char *
add_intern (char *str)
{
  struct context *c = threadlocal_get_context ();
  struct connection *conn = c ? c->conn : NULL;
  string_vector *list = conn ? &conn->interns : &global_interns;

  if (string_vector_append (list, str) == -1) {
    nbdkit_error ("malloc: %m");
    free (str);
    return NULL;
  }

  return str;
}

NBDKIT_DLL_PUBLIC const char *
nbdkit_strndup_intern (const char *str, size_t n)
{
  char *copy;

  if (str == NULL) {
    nbdkit_error ("nbdkit_strndup_intern: no string given");
    errno = EINVAL;
    return NULL;
  }

  copy = strndup (str, n);
  if (copy == NULL) {
    nbdkit_error ("strndup: %m");
    return NULL;
  }

  return add_intern (copy);
}

NBDKIT_DLL_PUBLIC const char *
nbdkit_strdup_intern (const char *str)
{
  char *copy;

  if (str == NULL) {
    nbdkit_error ("nbdkit_strdup_intern: no string given");
    errno = EINVAL;
    return NULL;
  }

  copy = strdup (str);
  if (copy == NULL) {
    nbdkit_error ("strdup: %m");
    return NULL;
  }

  return add_intern (copy);
}

NBDKIT_DLL_PUBLIC const char *
nbdkit_vprintf_intern (const char *fmt, va_list ap)
{
  char *str = NULL;

  if (vasprintf (&str, fmt, ap) == -1) {
    nbdkit_error ("asprintf: %m");
    return NULL;
  }

  return add_intern (str);
}

NBDKIT_DLL_PUBLIC const char *
nbdkit_printf_intern (const char *fmt, ...)
{
  va_list ap;
  const char *ret;

  va_start (ap, fmt);
  ret = nbdkit_vprintf_intern (fmt, ap);
  va_end (ap);
  return ret;
}

NBDKIT_DLL_PUBLIC void
nbdkit_disconnect (int force)
{
  struct connection *conn = threadlocal_get_conn ();

  if (!conn) {
    debug ("no connection in this thread, ignoring disconnect request");
    return;
  }
  if (connection_set_status (force ? STATUS_DEAD : STATUS_SHUTDOWN)) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&conn->write_lock);
    conn->close (SHUT_WR);
  }
}
