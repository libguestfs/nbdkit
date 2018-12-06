/* nbdkit
 * Copyright (C) 2018 Red Hat Inc.
 * All rights reserved.
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
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <nbdkit-plugin.h>

#include "call.h"

/* Ensure there is at least 1 byte of space in the buffer. */
static int
expand_buf (char **buf, size_t *buflen, size_t *bufalloc)
{
  char *nb;

  if (*bufalloc > *buflen)
    return 0;

  *bufalloc = *bufalloc == 0 ? 64 : *bufalloc * 2;
  nb = realloc (*buf, *bufalloc);
  if (nb == NULL) {
    nbdkit_error ("%s: malloc: %m", script);
    return -1;
  }
  *buf = nb;
  return 0;
}

/* This is the generic function that calls the script.  It can
 * optionally write to the script's stdin and read from the script's
 * stdout and stderr.  It returns the raw error code and does no error
 * processing.
 */
static int
call3 (const char *wbuf, size_t wbuflen, /* sent to stdin */
       char **rbuf, size_t *rbuflen,     /* read from stdout */
       char **ebuf, size_t *ebuflen,     /* read from stderr */
       const char **argv)                /* script + parameters */
{
  pid_t pid = -1;
  int status;
  int ret = ERROR;
  int in_fd[2] = { -1, -1 };
  int out_fd[2] = { -1, -1 };
  int err_fd[2] = { -1, -1 };
  size_t rbufalloc, ebufalloc;
  struct pollfd pfds[3];
  ssize_t r;

  *rbuf = *ebuf = NULL;
  *rbuflen = *ebuflen = 0;
  rbufalloc = ebufalloc = 0;

  if (pipe (in_fd) == -1) {
    nbdkit_error ("%s: pipe: %m", script);
    goto error;
  }
  if (pipe (out_fd) == -1) {
    nbdkit_error ("%s: pipe: %m", script);
    goto error;
  }
  if (pipe (err_fd) == -1) {
    nbdkit_error ("%s: pipe: %m", script);
    goto error;
  }

  pid = fork ();
  if (pid == -1) {
    nbdkit_error ("%s: fork: %m", script);
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

    /* Set $tmpdir for the script. */
    setenv ("tmpdir", tmpdir, 1);

    execvp (argv[0], (char **) argv);
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
      nbdkit_error ("%s: poll: %m", script);
      goto error;
    }

    /* Write more data to stdin. */
    if (pfds[0].revents & POLLOUT) {
      r = write (pfds[0].fd, wbuf, wbuflen);
      if (r == -1) {
        nbdkit_error ("%s: write: %m", script);
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
      if (expand_buf (rbuf, rbuflen, &rbufalloc) == -1)
        goto error;
      r = read (pfds[1].fd, *rbuf + *rbuflen, rbufalloc - *rbuflen);
      if (r == -1) {
        nbdkit_error ("%s: read: %m", script);
        goto error;
      }
      else if (r == 0) {
      close_out:
        close (out_fd[0]);
        out_fd[0] = -1;         /* poll will ignore this fd */
      }
      else if (r > 0)
        *rbuflen += r;
    }
    else if (pfds[1].revents & POLLHUP) {
      goto close_out;
    }

    /* Check stderr. */
    if (pfds[2].revents & POLLIN) {
      if (expand_buf (ebuf, ebuflen, &ebufalloc) == -1)
        goto error;
      r = read (pfds[2].fd, *ebuf + *ebuflen, ebufalloc - *ebuflen);
      if (r == -1) {
        nbdkit_error ("%s: read: %m", script);
        goto error;
      }
      else if (r == 0) {
      close_err:
        close (err_fd[0]);
        err_fd[0] = -1;         /* poll will ignore this fd */
      }
      else if (r > 0)
        *ebuflen += r;
    }
    else if (pfds[2].revents & POLLHUP) {
      goto close_err;
    }
  }

  if (waitpid (pid, &status, 0) == -1) {
    nbdkit_error ("%s: waitpid: %m", script);
    pid = -1;
    goto error;
  }
  pid = -1;

  if (WIFSIGNALED (status)) {
    nbdkit_error ("%s: script terminated by signal %d",
                  script, WTERMSIG (status));
    goto error;
  }

  if (WIFSTOPPED (status)) {
    nbdkit_error ("%s: script stopped by signal %d",
                  script, WTERMSIG (status));
    goto error;
  }

  /* \0-terminate both read buffers (for convenience). */
  if (expand_buf (rbuf, rbuflen, &rbufalloc) == -1)
    goto error;
  if (expand_buf (ebuf, ebuflen, &ebufalloc) == -1)
    goto error;
  (*rbuf)[*rbuflen] = '\0';
  (*ebuf)[*ebuflen] = '\0';

  ret = WEXITSTATUS (status);

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

static void
handle_script_error (char *ebuf, size_t len)
{
  int err;
  size_t skip;
  char *p;

  if (strcmp (ebuf, "EPERM ") == 0) {
    err = EPERM;
    skip = 6;
  }
  else if (strcmp (ebuf, "EIO ") == 0) {
    err = EIO;
    skip = 4;
  }
  else if (strcmp (ebuf, "ENOMEM ") == 0) {
    err = ENOMEM;
    skip = 7;
  }
  else if (strcmp (ebuf, "EINVAL ") == 0) {
    err = EINVAL;
    skip = 7;
  }
  else if (strcmp (ebuf, "ENOSPC ") == 0) {
    err = ENOSPC;
    skip = 7;
  }
  else if (strcmp (ebuf, "ESHUTDOWN ") == 0) {
    err = ESHUTDOWN;
    skip = 10;
  }
  else {
    /* Default to EIO. */
    err = EIO;
    skip = 0;
  }

  while (len > 0 && ebuf[len-1] == '\n')
    ebuf[--len] = '\0';

  if (len > 0) {
    p = strchr (ebuf, '\n');
    if (p) {
      /* More than one line, so write the whole message to debug ... */
      nbdkit_debug ("%s: %s", script, ebuf);
      /* ... but truncate it for the error message below. */
      *p = '\0';
    }
    if (strlen (ebuf) >= skip)
      ebuf += skip;
    nbdkit_error ("%s: %s", script, ebuf);
  }
  else
    nbdkit_error ("%s: script exited with error, but did not print an error message on stderr", script);

  /* Set errno. */
  errno = err;
}

/* Call the script with parameters.  Don't write to stdin or read from
 * stdout, but handle stderr if an error occurs.  Returns the exit
 * code from the script.
 */
exit_code
call (const char **argv)
{
  int r;
  char *rbuf;
  size_t rbuflen;
  char *ebuf;
  size_t ebuflen;

  r = call3 (NULL, 0, &rbuf, &rbuflen, &ebuf, &ebuflen, argv);
  switch (r) {
  case OK:
  case MISSING:
  case RET_FALSE:
    /* Script successful. */
    free (rbuf);
    free (ebuf);
    return r;

  case ERROR:
  default:
    /* Error case. */
    free (rbuf);
    handle_script_error (ebuf, ebuflen);
    free (ebuf);
    return ERROR;
  }
}

/* Call the script with parameters.  Read from stdout and return the
 * buffer.  Returns the exit code from the script.
 */
exit_code
call_read (char **rbuf, size_t *rbuflen, const char **argv)
{
  int r;
  char *ebuf;
  size_t ebuflen;

  r = call3 (NULL, 0, rbuf, rbuflen, &ebuf, &ebuflen, argv);
  switch (r) {
  case OK:
  case MISSING:
  case RET_FALSE:
    /* Script successful. */
    free (ebuf);
    return r;

  case ERROR:
  default:
    /* Error case. */
    free (*rbuf);
    *rbuf = NULL;
    handle_script_error (ebuf, ebuflen);
    free (ebuf);
    return ERROR;
  }
}

/* Call the script with parameters.  Write to stdin of the script.
 * Returns the exit code from the script.
 */
exit_code
call_write (const char *wbuf, size_t wbuflen, const char **argv)
{
  int r;
  char *rbuf;
  size_t rbuflen;
  char *ebuf;
  size_t ebuflen;

  r = call3 (wbuf, wbuflen, &rbuf, &rbuflen, &ebuf, &ebuflen, argv);
  switch (r) {
  case OK:
  case MISSING:
  case RET_FALSE:
    /* Script successful. */
    free (rbuf);
    free (ebuf);
    return r;

  case ERROR:
  default:
    /* Error case. */
    free (rbuf);
    handle_script_error (ebuf, ebuflen);
    free (ebuf);
    return ERROR;
  }
}
