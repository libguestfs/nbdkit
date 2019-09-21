/* nbdkit
 * Copyright (C) 2019 Red Hat Inc.
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
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "internal.h"

/* Handle socket activation.  This is controlled through special
 * environment variables inherited by nbdkit.  Returns 0 if no socket
 * activation.  Otherwise returns the number of FDs.  See also
 * virGetListenFDs in libvirt.org:src/util/virutil.c
 */
unsigned int
get_socket_activation (void)
{
  const char *s;
  unsigned int pid;
  unsigned int nr_fds;
  unsigned int i;
  int fd;

  s = getenv ("LISTEN_PID");
  if (s == NULL)
    return 0;
  if (nbdkit_parse_unsigned ("LISTEN_PID", s, &pid) == -1)
    return 0;
  if (pid != getpid ()) {
    fprintf (stderr, "%s: %s was not for us (ignored)\n",
             program_name, "LISTEN_PID");
    return 0;
  }

  s = getenv ("LISTEN_FDS");
  if (s == NULL)
    return 0;
  if (nbdkit_parse_unsigned ("LISTEN_FDS", s, &nr_fds) == -1)
    return 0;

  /* So these are not passed to any child processes we might start. */
  unsetenv ("LISTEN_FDS");
  unsetenv ("LISTEN_PID");

  /* So the file descriptors don't leak into child processes. */
  for (i = 0; i < nr_fds; ++i) {
    fd = FIRST_SOCKET_ACTIVATION_FD + i;
    if (fcntl (fd, F_SETFD, FD_CLOEXEC) == -1) {
      /* If we cannot set FD_CLOEXEC then it probably means the file
       * descriptor is invalid, so socket activation has gone wrong
       * and we should exit.
       */
      fprintf (stderr, "%s: socket activation: "
               "invalid file descriptor fd = %d: %s\n",
               program_name, fd, strerror(errno));
      exit (EXIT_FAILURE);
    }
  }

  return nr_fds;
}
