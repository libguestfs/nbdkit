/* nbdkit
 * Copyright (C) 2013-2020 Red Hat Inc.
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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <termios.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>

#include "ascii-ctype.h"
#include "ascii-string.h"
#include "get-current-dir-name.h"
#include "getline.h"
#include "realpath.h"

#include "internal.h"

char *
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

  if (asprintf (&ret, "%s/%s", pwd, path) == -1) {
    nbdkit_error ("asprintf: %m");
    return NULL;
  }

  return ret;
}

char *
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
int
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

int
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

int
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

int
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

int
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

int
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

int
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

int
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

int
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

int
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
int64_t
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
int
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
int
nbdkit_stdio_safe (void)
{
  return !listen_stdin && !configured;
}

/* Read a password from configuration value. */
static int read_password_interactive (char **password);
static int read_password_from_fd (const char *what, int fd, char **password);

int
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
    int fd;

    if (nbdkit_parse_int ("password file descriptor", &value[1], &fd) == -1)
      return -1;
    if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO) {
      nbdkit_error ("cannot use password -FD for stdin/stdout/stderr");
      return -1;
    }
    if (read_password_from_fd (&value[1], fd, password) == -1)
      return -1;
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

int
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
  if (sigfillset(&all))
    abort ();
  switch (ppoll (fds, sizeof fds / sizeof fds[0], &ts, &all)) {
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
  assert (quit ||
          (conn && conn->nworkers > 0 && connection_get_status () < 1) ||
          (conn && (fds[2].revents & (POLLRDHUP | POLLHUP | POLLERR))));
  nbdkit_error ("aborting sleep to shut down");
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
const char *
nbdkit_export_name (void)
{
  struct connection *conn = threadlocal_get_conn ();

  if (!conn) {
    nbdkit_error ("no connection in this thread");
    return NULL;
  }

  return conn->exportname;
}

/* This function will be deprecated for API V3 users.  The preferred
 * approach will be to get the tls mode from .open().
 */
int
nbdkit_is_tls (void)
{
  struct connection *conn = threadlocal_get_conn ();

  if (!conn) {
    nbdkit_error ("no connection in this thread");
    return -1;
  }

  return conn->using_tls;
}

int
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
