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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "vector.h"

#include "vddk.h"

char *reexeced;                 /* orig LD_LIBRARY_PATH on reexec */

DEFINE_VECTOR_TYPE(string_vector, char *);

#define CLEANUP_FREE_STRING_VECTOR \
  __attribute__((cleanup (cleanup_free_string_vector)))

static void
cleanup_free_string_vector (string_vector *v)
{
  string_vector_iter (v, (void *) free);
  free (v->ptr);
}

/* Perform a re-exec that temporarily modifies LD_LIBRARY_PATH.  Does
 * not return on success; on failure, problems have been logged, but
 * the caller prefers to proceed as if this had not been attempted.
 * Thus, no return value is needed.
 */
static void
perform_reexec (const char *env, const char *prepend)
{
  CLEANUP_FREE char *library = NULL;
  CLEANUP_FREE_STRING_VECTOR string_vector argv = empty_vector;
  int fd;
  size_t len = 0, buflen = 512;
  CLEANUP_FREE char *buf = NULL;

  /* In order to re-exec, we need our original command line.  The
   * Linux kernel does not make it easy to know in advance how large
   * it was, so we just slurp in the whole file, doubling our reads
   * until we get a short read.  This assumes nbdkit did not alter its
   * original argv[].
   */
  fd = open ("/proc/self/cmdline", O_RDONLY);
  if (fd == -1) {
    nbdkit_debug ("failure to parse original argv: %m");
    return;
  }

  do {
    char *p = realloc (buf, buflen * 2);
    ssize_t r;

    if (!p) {
      nbdkit_debug ("failure to parse original argv: %m");
      return;
    }
    buf = p;
    buflen *= 2;
    r = read (fd, buf + len, buflen - len);
    if (r == -1) {
      nbdkit_debug ("failure to parse original argv: %m");
      return;
    }
    len += r;
  } while (len == buflen);
  nbdkit_debug ("original command line occupies %zu bytes", len);

  /* Split cmdline into argv, then append one more arg. */
  buflen = len;
  len = 0;
  while (len < buflen) {
    if (string_vector_append (&argv, buf + len) == -1) {
    argv_realloc_fail:
      nbdkit_debug ("argv: realloc: %m");
      return;
    }
    len += strlen (buf + len) + 1;
  }
  if (!env)
    env = "";
  nbdkit_debug ("adding reexeced_=%s", env);
  if (asprintf (&reexeced, "reexeced_=%s", env) == -1)
    goto argv_realloc_fail;
  if (string_vector_append (&argv, reexeced) == -1)
    goto argv_realloc_fail;
  if (string_vector_append (&argv, NULL) == -1)
    goto argv_realloc_fail;

  if (env[0]) {
    if (asprintf (&library, "%s:%s", prepend, env) == -1)
      assert (library == NULL);
  }
  else
    library = strdup (prepend);
  if (!library || setenv ("LD_LIBRARY_PATH", library, 1) == -1) {
    nbdkit_debug ("failure to set LD_LIBRARY_PATH: %m");
    return;
  }

  nbdkit_debug ("re-executing with updated LD_LIBRARY_PATH=%s", library);
  fflush (NULL);
  execvp ("/proc/self/exe", argv.ptr);
  nbdkit_debug ("failure to execvp: %m");
}

/* See if prepend is already in LD_LIBRARY_PATH; if not, re-exec. */
void
reexec_if_needed (const char *prepend)
{
  const char *env = getenv ("LD_LIBRARY_PATH");
  CLEANUP_FREE char *haystack = NULL;
  CLEANUP_FREE char *needle = NULL;

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
