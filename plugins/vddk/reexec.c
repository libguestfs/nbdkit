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
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/types.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "const-string-vector.h"
#include "nbdkit-string.h"

#include "vddk.h"

bool noreexec = false;          /* hidden noreexec option */
char *reexeced;                 /* orig LD_LIBRARY_PATH on reexec */

/* Perform a re-exec that temporarily modifies LD_LIBRARY_PATH.  Does
 * not return on success.  Some failures such as /proc/self/... not
 * present are not errors - it means we are not on a Linux-like
 * platform, VDDK probably doesn't work anyway, and we simply return.
 * Memory allocation failures etc result in an exit.
 */
static void
perform_reexec (const char *env, const char *prepend)
{
  static const char cmdline_file[] = "/proc/self/cmdline";
  static const char exe_file[] = "/proc/self/exe";
  CLEANUP_FREE char *library = NULL;
  CLEANUP_FREE_STRING string buf = empty_vector;
  CLEANUP_FREE_CONST_STRING_VECTOR const_string_vector argv = empty_vector;
  int fd;
  size_t len;
  bool seen_password = false;
  char tmpfile[] = "/tmp/XXXXXX";
  CLEANUP_FREE char *password_fd = NULL;

  /* In order to re-exec, we need our original command line.  The
   * Linux kernel does not make it easy to know in advance how large
   * it was, so we just slurp in the whole file, doubling our reads
   * until we get a short read.  This assumes nbdkit did not alter its
   * original argv[].
   */
  fd = open (cmdline_file, O_RDONLY|O_CLOEXEC);
  if (fd == -1) {
    /* Not an error. */
    nbdkit_debug ("open: %s: %m", cmdline_file);
    return;
  }

  for (;;) {
    ssize_t r;

    if (string_reserve (&buf, 512) == -1) {
      nbdkit_error ("realloc: %m");
      exit (EXIT_FAILURE);
    }
    r = read (fd, buf.ptr + buf.len, buf.cap - buf.len);
    if (r == -1) {
      nbdkit_error ("read: %s: %m", cmdline_file);
      exit (EXIT_FAILURE);
    }
    if (r == 0)
      break;
    buf.len += r;
  }
  close (fd);
  nbdkit_debug ("original command line occupies %zu bytes", buf.len);

  /* Split cmdline into argv, then append one more arg. */
  for (len = 0; len < buf.len; len += strlen (buf.ptr + len) + 1) {
    char *arg = buf.ptr + len;  /* Next \0-terminated argument. */

    /* See below for why we eat password parameter(s). */
    if (strncmp (arg, "password=", 9) == 0)
      seen_password = true;
    else {
      if (const_string_vector_append (&argv, arg) == -1) {
      argv_realloc_fail:
        nbdkit_error ("argv: realloc: %m");
        exit (EXIT_FAILURE);
      }
    }
  }

  /* password parameter requires special handling for reexec.  For
   * password=- and password=-FD, after reexec we might try to
   * reread these, but stdin has gone away and FD has been consumed
   * already so that won't work.  Even password=+FILE is a little
   * problematic since the file will be read twice, which may break
   * for special files.
   *
   * However we may write the password to a temporary file and
   * substitute password=-<FD> of the opened temporary file here.
   * The trick is described by Eric Blake here:
   * https://www.redhat.com/archives/libguestfs/2020-June/msg00021.html
   *
   * (RHBZ#1842440)
   */
  if (seen_password && password) {
    fd = mkstemp (tmpfile);
    if (fd == -1) {
      nbdkit_error ("mkstemp: %m");
      exit (EXIT_FAILURE);
    }
    unlink (tmpfile);
    if (write (fd, password, strlen (password)) != strlen (password)) {
      nbdkit_error ("write: %m");
      exit (EXIT_FAILURE);
    }
    lseek (fd, 0, SEEK_SET);
    if (asprintf (&password_fd, "password=-%d", fd) == -1) {
      nbdkit_error ("asprintf: %m");
      exit (EXIT_FAILURE);
    }
    if (const_string_vector_append (&argv, password_fd) == -1)
      goto argv_realloc_fail;
  }

  if (!env)
    env = "";
  nbdkit_debug ("adding reexeced_=%s", env);
  if (asprintf (&reexeced, "reexeced_=%s", env) == -1)
    goto argv_realloc_fail;
  if (const_string_vector_append (&argv, reexeced) == -1)
    goto argv_realloc_fail;
  if (const_string_vector_append (&argv, NULL) == -1)
    goto argv_realloc_fail;

  if (env[0]) {
    if (asprintf (&library, "%s:%s", prepend, env) == -1)
      assert (library == NULL);
  }
  else
    library = strdup (prepend);
  if (!library || setenv ("LD_LIBRARY_PATH", library, 1) == -1) {
    nbdkit_error ("failure to set LD_LIBRARY_PATH: %m");
    exit (EXIT_FAILURE);
  }

  nbdkit_debug ("re-executing with updated LD_LIBRARY_PATH=%s", library);
  fflush (NULL);
  execvp (exe_file, (char **) argv.ptr);
  nbdkit_debug ("execvp: %s: %m", exe_file);
  /* Not an error. */
}

/* See if prepend is already in LD_LIBRARY_PATH; if not, re-exec. */
void
reexec_if_needed (const char *prepend)
{
  const char *env = getenv ("LD_LIBRARY_PATH");
  CLEANUP_FREE char *haystack = NULL;
  CLEANUP_FREE char *needle = NULL;

  if (noreexec)
    return;
  if (reexeced)
    return;
  if (env && asprintf (&haystack, ":%s:", env) >= 0 &&
      asprintf (&needle, ":%s:", prepend) >= 0 &&
      strstr (haystack, needle) != NULL)
    return;

  perform_reexec (env, prepend);
}

/* If load_library caused a re-execution with an expanded
 * LD_LIBRARY_PATH, restore it back to its original contents, passed
 * as the value of "reexeced_".  dlopen uses the value of
 * LD_LIBRARY_PATH cached at program startup; our change is for the
 * sake of child processes (such as --run) to see the same
 * environment as the original nbdkit saw before re-exec.
 */
int
restore_ld_library_path (void)
{
  if (reexeced) {
    char *env = getenv ("LD_LIBRARY_PATH");

    nbdkit_debug ("cleaning up after re-exec");
    if (!env || strstr (env, reexeced) == NULL ||
        (libdir && strncmp (env, libdir, strlen (libdir)) != 0)) {
      nbdkit_error ("'reexeced_' set with garbled environment");
      return -1;
    }
    if (reexeced[0]) {
      if (setenv ("LD_LIBRARY_PATH", reexeced, 1) == -1) {
        nbdkit_error ("setenv: %m");
        return -1;
      }
    }
    else if (unsetenv ("LD_LIBRARY_PATH") == -1) {
      nbdkit_error ("unsetenv: %m");
      return -1;
    }
  }

  return 0;
}
