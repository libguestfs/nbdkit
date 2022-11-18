/* nbdkit
 * Copyright (C) 2018-2022 Red Hat Inc.
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

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <nbdkit-plugin.h>

#include "ascii-ctype.h"
#include "ascii-string.h"
#include "cleanup.h"
#include "utils.h"
#include "vector.h"

#include "call.h"

#ifndef HAVE_ENVIRON_DECL
extern char **environ;
#endif

/* Temporary directory for scripts to use. */
char tmpdir[] = "/tmp/nbdkitXXXXXX";

/* Private copy of environ, with $tmpdir added. */
static char **env;

void
call_load (void)
{
  /* Create the temporary directory for the shell script to use. */
  if (mkdtemp (tmpdir) == NULL) {
    nbdkit_error ("mkdtemp: /tmp: %m");
    exit (EXIT_FAILURE);
  }

  nbdkit_debug ("load: tmpdir: %s", tmpdir);

  /* Copy the environment, and add $tmpdir. */
  env = copy_environ (environ, "tmpdir", tmpdir, NULL);
  if (env == NULL)
    exit (EXIT_FAILURE);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
void
call_unload (void)
{
  CLEANUP_FREE char *cmd = NULL;
  size_t i;

  /* Delete the temporary directory.  Ignore all errors. */
  if (asprintf (&cmd, "rm -rf %s", tmpdir) >= 0)
    system (cmd);

  /* Free the private copy of environ. */
  for (i = 0; env[i] != NULL; ++i)
    free (env[i]);
  free (env);
}
#pragma GCC diagnostic pop

static void
debug_call (const char **argv)
{
  CLEANUP_FREE char *debug = NULL;
  size_t i, len = 0;
  FILE *fp;

  fp = open_memstream (&debug, &len);
  if (fp == NULL)
    return;

  fprintf (fp, "calling:");
  for (i = 0; argv[i] != NULL; ++i) {
    fputc (' ', fp);
    shell_quote (argv[i], fp);
  }

  fclose (fp);

  nbdkit_debug ("%s", debug);
}

/* This is the generic function that calls the script.  It can
 * optionally write to the script's stdin and read from the script's
 * stdout and stderr.  It returns the raw error code and does no error
 * processing.
 */
static int
call3 (const char *wbuf, size_t wbuflen, /* sent to stdin (can be NULL) */
       string *rbuf,                     /* read from stdout */
       string *ebuf,                     /* read from stderr */
       const char **argv)                /* script + parameters */
{
  const char *argv0 = argv[0]; /* script name, used in error messages */
#ifndef __GLIBC__
  CLEANUP_FREE const char **sh_argv = NULL;
  size_t i;
#endif
  pid_t pid = -1;
  int status;
  int ret = ERROR;
  int in_fd[2] = { -1, -1 };
  int out_fd[2] = { -1, -1 };
  int err_fd[2] = { -1, -1 };
  struct pollfd pfds[3];
  ssize_t r;

  /* Ignore any previous contents of rbuf, ebuf. */
  string_reset (rbuf);
  string_reset (ebuf);

  debug_call (argv);

#ifdef HAVE_PIPE2
  if (pipe2 (in_fd, O_CLOEXEC) == -1) {
    nbdkit_error ("%s: pipe2: %m", argv0);
    goto error;
  }
  if (pipe2 (out_fd, O_CLOEXEC) == -1) {
    nbdkit_error ("%s: pipe2: %m", argv0);
    goto error;
  }
  if (pipe2 (err_fd, O_CLOEXEC) == -1) {
    nbdkit_error ("%s: pipe2: %m", argv0);
    goto error;
  }
#else
  /* Without pipe2, nbdkit forces the thread model maximum down to
   * NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS, this in turn ensures
   * no other thread will be trying to fork, and thus we can skip
   * worrying about CLOEXEC races.  Therefore, it's not worth adding a
   * loop after fork to close unexpected fds.
   */
  if (pipe (in_fd) == -1) {
    nbdkit_error ("%s: pipe: %m", argv0);
    goto error;
  }
  if (pipe (out_fd) == -1) {
    nbdkit_error ("%s: pipe: %m", argv0);
    goto error;
  }
  if (pipe (err_fd) == -1) {
    nbdkit_error ("%s: pipe: %m", argv0);
    goto error;
  }
#endif

  /* Ensure that stdin/out/err of the current process were not empty
   * before we started creating pipes (otherwise, the close and dup2
   * calls below become more complex to juggle fds around correctly).
  */
  assert (in_fd[0] > STDERR_FILENO && in_fd[1] > STDERR_FILENO &&
          out_fd[0] > STDERR_FILENO && out_fd[1] > STDERR_FILENO &&
          err_fd[0] > STDERR_FILENO && err_fd[1] > STDERR_FILENO);

#ifndef __GLIBC__
  /* glibc contains a workaround for scripts which don't have a
   * shebang.  See maybe_script_execute in glibc posix/execvpe.c.
   * We rely on this in nbdkit, so if not using glibc we emulate it.
   * Note this is tested when we do CI on Alpine (which uses musl).
   */
  /* Count the number of arguments, ignoring script name. */
  for (i = 2; argv[i]; i++)
    ;
  sh_argv = calloc (i + 2 /* /bin/sh + NULL */, sizeof (const char *));
  if (sh_argv == NULL) {
    nbdkit_error ("%s: calloc: %m", argv0);
    goto error;
  }
  sh_argv[0] = "/bin/sh";
  for (i = 0; argv[i]; i++)
    sh_argv[i+1] = argv[i];
#endif

  pid = fork ();
  if (pid == -1) {
    nbdkit_error ("%s: fork: %m", argv0);
    goto error;
  }

  if (pid == 0) {               /* Child. */
    close (in_fd[1]);
    close (out_fd[0]);
    close (err_fd[0]);
    dup2 (in_fd[0], 0);
    dup2 (out_fd[1], 1);
    dup2 (err_fd[1], 2);
    close (in_fd[0]);
    close (out_fd[1]);
    close (err_fd[1]);

    /* Restore SIGPIPE back to SIG_DFL, since shell can't undo SIG_IGN */
    signal (SIGPIPE, SIG_DFL);

    /* Note the assignment of environ avoids using execvpe which is a
     * GNU extension.  See also:
     * https://github.com/libguestfs/libnbd/commit/dc64ac5cdd0bc80ca4e18935ad0e8801d11a8644
     */
    environ = env;
    execvp (argv[0], (char **) argv);
#ifndef __GLIBC__
    /* Non-glibc workaround for missing shebang - see above. */
    if (errno == ENOEXEC)
      execvp (sh_argv[0], (char **) sh_argv);
#endif
    perror (argv[0]);
    _exit (EXIT_FAILURE);
  }

  /* Parent. */
  close (in_fd[0]);  in_fd[0] = -1;
  close (out_fd[1]); out_fd[1] = -1;
  close (err_fd[1]); err_fd[1] = -1;

  while (out_fd[0] >= 0 || err_fd[0] >= 0) {
    pfds[0].fd = in_fd[1];      /* Connected to child stdin. */
    pfds[0].events = wbuflen ? POLLOUT : 0;
    pfds[1].fd = out_fd[0];     /* Connected to child stdout. */
    pfds[1].events = POLLIN;
    pfds[2].fd = err_fd[0];     /* Connected to child stderr. */
    pfds[2].events = POLLIN;

    if (poll (pfds, 3, -1) == -1) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      nbdkit_error ("%s: poll: %m", argv0);
      goto error;
    }

    /* Write more data to stdin. */
    if (pfds[0].revents & POLLOUT) {
      r = write (pfds[0].fd, wbuf, wbuflen);
      if (r == -1) {
        if (errno == EPIPE) {
          /* We tried to write to the script but it didn't consume
           * the data.  Probably the script exited without reading
           * from stdin.  This is an error in the script.
           */
          nbdkit_error ("%s: write to script failed because of a broken pipe: "
                        "this can happen if the script exits without "
                        "consuming stdin, which usually indicates a bug "
                        "in the script",
                        argv0);
        }
        else
          nbdkit_error ("%s: write: %m", argv0);
        goto error;
      }
      wbuf += r;
      wbuflen -= r;
      /* After writing all the data we close the pipe so that
       * the reader on the other end doesn't wait for more.
       */
      if (wbuflen == 0) {
        close (in_fd[1]);
        in_fd[1] = -1;          /* poll will ignore this fd */
      }
    }

    /* Check stdout. */
    if (pfds[1].revents & POLLIN) {
      if (rbuf->cap <= rbuf->len && string_reserve (rbuf, 64) == -1) {
        nbdkit_error ("%s: realloc: %m", argv0);
        goto error;
      }
      r = read (pfds[1].fd, &rbuf->ptr[rbuf->len], rbuf->cap - rbuf->len);
      if (r == -1) {
        nbdkit_error ("%s: read: %m", argv0);
        goto error;
      }
      else if (r == 0) {
      close_out:
        close (out_fd[0]);
        out_fd[0] = -1;         /* poll will ignore this fd */
      }
      else if (r > 0)
        rbuf->len += r;
    }
    else if (pfds[1].revents & POLLHUP) {
      goto close_out;
    }

    /* Check stderr. */
    if (pfds[2].revents & POLLIN) {
      if (ebuf->cap <= ebuf->len && string_reserve (ebuf, 64) == -1) {
        nbdkit_error ("%s: realloc: %m", argv0);
        goto error;
      }
      r = read (pfds[2].fd, &ebuf->ptr[ebuf->len], ebuf->cap - ebuf->len);
      if (r == -1) {
        nbdkit_error ("%s: read: %m", argv0);
        goto error;
      }
      else if (r == 0) {
      close_err:
        close (err_fd[0]);
        err_fd[0] = -1;         /* poll will ignore this fd */
      }
      else if (r > 0)
        ebuf->len += r;
    }
    else if (pfds[2].revents & POLLHUP) {
      goto close_err;
    }
  }

  if (waitpid (pid, &status, 0) == -1) {
    nbdkit_error ("%s: waitpid: %m", argv0);
    pid = -1;
    goto error;
  }
  pid = -1;

  if (WIFSIGNALED (status)) {
    nbdkit_error ("%s: script terminated by signal %d",
                  argv0, WTERMSIG (status));
    goto error;
  }

  if (WIFSTOPPED (status)) {
    nbdkit_error ("%s: script stopped by signal %d",
                  argv0, WTERMSIG (status));
    goto error;
  }

  /* \0-terminate both read buffers (for convenience). */
  if ((rbuf->cap <= rbuf->len && string_reserve (rbuf, 1) == -1) ||
      (ebuf->cap <= ebuf->len && string_reserve (ebuf, 1) == -1)) {
    nbdkit_error ("%s: realloc: %m", argv0);
    goto error;
  }
  rbuf->ptr[rbuf->len] = '\0';
  ebuf->ptr[ebuf->len] = '\0';

  ret = WEXITSTATUS (status);
  nbdkit_debug ("completed: %s %s: status %d", argv0, argv[1], ret);

 error:
  if (in_fd[0] >= 0)
    close (in_fd[0]);
  if (in_fd[1] >= 0)
    close (in_fd[1]);
  if (out_fd[0] >= 0)
    close (out_fd[0]);
  if (out_fd[1] >= 0)
    close (out_fd[1]);
  if (err_fd[0] >= 0)
    close (err_fd[0]);
  if (err_fd[1] >= 0)
    close (err_fd[1]);
  if (pid >= 0)
    waitpid (pid, NULL, 0);

  return ret;
}

/* Normalize return codes and parse error string. */
static exit_code
handle_script_error (const char *argv0, string *ebuf, exit_code code)
{
  int err;
  size_t skip = 0;
  char *p;

  /* ebuf->ptr might be NULL on some return paths from call3().  To
   * make the following code easier, allocate it and reserve one byte.
   * Note that ebuf->len is still 0 after this.
   */
  if (ebuf->len == 0) {
    if (string_reserve (ebuf, 1) == -1) {
      nbdkit_error ("realloc: %m");
      err = EIO;
      return ERROR;
    }
    ebuf->ptr[ebuf->len] = '\0';
  }

  switch (code) {
  case OK:
  case MISSING:
  case RET_FALSE:
    /* Script successful. */
    return code;

  case ERROR:
  default:
    err = EIO;
    break;
  }

  /* Recognize the errno values that match NBD protocol errors */
  if (ascii_strncasecmp (ebuf->ptr, "EPERM", 5) == 0) {
    err = EPERM;
    skip = 5;
  }
  else if (ascii_strncasecmp (ebuf->ptr, "EIO", 3) == 0) {
    err = EIO;
    skip = 3;
  }
  else if (ascii_strncasecmp (ebuf->ptr, "ENOMEM", 6) == 0) {
    err = ENOMEM;
    skip = 6;
  }
  else if (ascii_strncasecmp (ebuf->ptr, "EINVAL", 6) == 0) {
    err = EINVAL;
    skip = 6;
  }
  else if (ascii_strncasecmp (ebuf->ptr, "ENOSPC", 6) == 0) {
    err = ENOSPC;
    skip = 6;
  }
  else if (ascii_strncasecmp (ebuf->ptr, "EOVERFLOW", 9) == 0) {
    err = EOVERFLOW;
    skip = 9;
  }
  else if (ascii_strncasecmp (ebuf->ptr, "ESHUTDOWN", 9) == 0) {
    err = ESHUTDOWN;
    skip = 9;
  }
  else if (ascii_strncasecmp (ebuf->ptr, "ENOTSUP", 7) == 0) {
    err = ENOTSUP;
    skip = 7;
  }
  else if (ascii_strncasecmp (ebuf->ptr, "EOPNOTSUPP", 10) == 0) {
    err = EOPNOTSUPP;
    skip = 10;
  }
  /* Other errno values that server/protocol.c treats specially */
  else if (ascii_strncasecmp (ebuf->ptr, "EROFS", 5) == 0) {
    err = EROFS;
    skip = 5;
  }
  else if (ascii_strncasecmp (ebuf->ptr, "EDQUOT", 6) == 0) {
#ifdef EDQUOT
    err = EDQUOT;
#else
    err = ENOSPC;
#endif
    skip = 6;
  }
  else if (ascii_strncasecmp (ebuf->ptr, "EFBIG", 5) == 0) {
    err = EFBIG;
    skip = 5;
  }
  /* Otherwise, use value of err populated in switch above */

  if (skip && ebuf->ptr[skip]) {
    if (!ascii_isspace (ebuf->ptr[skip])) {
      /* Treat 'EINVALID' as EIO, not EINVAL */
      err = EIO;
      skip = 0;
    }
    else
      do
        skip++;
      while (ascii_isspace (ebuf->ptr[skip]));
  }

  while (ebuf->len > 0 && ebuf->ptr[ebuf->len-1] == '\n')
    ebuf->ptr[--ebuf->len] = '\0';

  if (ebuf->len > 0) {
    p = strchr (&ebuf->ptr[skip], '\n');
    if (p) {
      /* More than one line, so write the whole message to debug ... */
      nbdkit_debug ("%s: %s", argv0, ebuf->ptr);
      /* ... but truncate it for the error message below. */
      *p = '\0';
    }
    nbdkit_error ("%s: %s", argv0, &ebuf->ptr[skip]);
  }
  else {
    nbdkit_error ("%s: script exited with error, "
                  "but did not print an error message on stderr", argv0);
  }

  /* Set errno. */
  errno = err;
  return ERROR;
}

/* Call the script with parameters.  Don't write to stdin or read from
 * stdout, but handle stderr if an error occurs.  Returns the exit
 * code from the script.
 */
exit_code
call (const char **argv)
{
  int r;
  CLEANUP_FREE_STRING string rbuf = empty_vector;
  CLEANUP_FREE_STRING string ebuf = empty_vector;

  r = call3 (NULL, 0, &rbuf, &ebuf, argv);
  return handle_script_error (argv[0], &ebuf, r);
}

/* Call the script with parameters.  Read from stdout and return the
 * buffer.  Returns the exit code from the script.
 */
exit_code
call_read (string *rbuf, const char **argv)
{
  int r;
  CLEANUP_FREE_STRING string ebuf = empty_vector;

  r = call3 (NULL, 0, rbuf, &ebuf, argv);
  r = handle_script_error (argv[0], &ebuf, r);
  if (r == ERROR)
    string_reset (rbuf);
  return r;
}

/* Call the script with parameters.  Write to stdin of the script.
 * Returns the exit code from the script.
 */
exit_code
call_write (const char *wbuf, size_t wbuflen, const char **argv)
{
  int r;
  CLEANUP_FREE_STRING string rbuf = empty_vector;
  CLEANUP_FREE_STRING string ebuf = empty_vector;

  r = call3 (wbuf, wbuflen, &rbuf, &ebuf, argv);
  return handle_script_error (argv[0], &ebuf, r);
}
