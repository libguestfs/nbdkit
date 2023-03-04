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
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include <nbdkit-plugin.h>

#include "windows-compat.h"

#ifndef WIN32

/* Convert exit status to nbd_error.  If the exit status was nonzero
 * or another failure then -1 is returned.
 */
int
exit_status_to_nbd_error (int status, const char *cmd)
{
  if (WIFEXITED (status) && WEXITSTATUS (status) != 0) {
    nbdkit_error ("%s: command failed with exit code %d",
                  cmd, WEXITSTATUS (status));
    return -1;
  }
  else if (WIFSIGNALED (status)) {
    nbdkit_error ("%s: command was killed by signal %d",
                  cmd, WTERMSIG (status));
    return -1;
  }
  else if (WIFSTOPPED (status)) {
    nbdkit_error ("%s: command was stopped by signal %d",
                  cmd, WSTOPSIG (status));
    return -1;
  }

  return 0;
}

#else /* WIN32 */

/* This assumes we're using Win32 system().  See:
 * https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/system-wsystem?view=vs-2019
 */
int
exit_status_to_nbd_error (int status, const char *cmd)
{
  if (status == 0)
    return 0;

  nbdkit_error ("%s: command failed: errno = %d", cmd, errno);
  return -1;
}

#endif /* WIN32 */

#ifndef WIN32

/* Set the FD_CLOEXEC flag on the given fd, if it is non-negative.
 * On failure, close fd and return -1; on success, return fd.
 *
 * Note that this function should ONLY be used on platforms that lack
 * atomic CLOEXEC support during fd creation (such as Haiku in 2019);
 * when using it as a fallback path, you must also consider how to
 * prevent fd leaks to plugins that want to fork().
 */
int
set_cloexec (int fd)
{
#if (defined SOCK_CLOEXEC && defined HAVE_MKOSTEMP && defined HAVE_PIPE2 && \
     defined HAVE_ACCEPT4)
  nbdkit_error ("prefer creating fds with CLOEXEC atomically set");
  close (fd);
  errno = EBADF;
  return -1;
#else
# if !defined __APPLE__ && \
     (defined SOCK_CLOEXEC || defined HAVE_MKOSTEMP || defined HAVE_PIPE2 || \
      defined HAVE_ACCEPT4)
# error "Unexpected: your system has incomplete atomic CLOEXEC support"
# endif
  int f;
  int err;

  if (fd == -1)
    return -1;

  f = fcntl (fd, F_GETFD);
  if (f == -1 || fcntl (fd, F_SETFD, f | FD_CLOEXEC) == -1) {
    err = errno;
    nbdkit_error ("fcntl: %m");
    close (fd);
    errno = err;
    return -1;
  }
  return fd;
#endif
}

#else /* WIN32 */

int
set_cloexec (int fd)
{
  return fd;
}

#endif /* WIN32 */

#ifndef WIN32

/* Set the O_NONBLOCK flag on the given fd, if it is non-negative.
 * On failure, close fd and return -1; on success, return fd.
 */
int
set_nonblock (int fd)
{
  int f;
  int err;

  if (fd == -1)
    return -1;

  f = fcntl (fd, F_GETFL);
  if (f == -1 || fcntl (fd, F_SETFL, f | O_NONBLOCK) == -1) {
    err = errno;
    nbdkit_error ("fcntl: %m");
    close (fd);
    errno = err;
    return -1;
  }
  return fd;
}

#else /* WIN32 */

int
set_nonblock (int fd)
{
  return fd;
}

#endif /* WIN32 */

#ifndef WIN32

char *
make_temporary_directory (void)
{
  char template[] = "/tmp/nbdkitXXXXXX";

  if (mkdtemp (template) == NULL)
    return NULL;

  return strdup (template);
}

#else /* WIN32 */

char *
make_temporary_directory (void)
{
  char tmppath[MAX_PATH];
  char tmpname[MAX_PATH];
  DWORD ret;

  ret = GetTempPath (MAX_PATH, tmppath);
  if (ret > MAX_PATH || ret == 0) {
    fprintf (stderr, "mkdtemp: GetTempPath: %lu\n", GetLastError ());
    return NULL;
  }

  ret = GetTempFileName (tmppath, TEXT ("nbdkit"), 0, tmpname);
  if (!ret) {
    fprintf (stderr, "mkdtemp: GetTempFileName: %lu\n", GetLastError ());
    return NULL;
  }

  /* The above function actually creates the file, so we must remove
   * it before creating the directory.  Not ideal because it leaves a
   * small window for exploitation (XXX).
   */
  unlink (tmpname);

  if (mkdir (tmpname) == -1) {
    fprintf (stderr, "mkdtemp: mkdir: %s: %lu\n", tmpname, GetLastError ());
    return NULL;
  }

  return strdup (tmpname);
}

#endif /* WIN32 */
