/* nbdkit
 * Copyright (C) 2013-2018 Red Hat Inc.
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
#include <errno.h>

#include "internal.h"

/* Note: preserves the previous value of errno. */
void
log_stderr_verror (const char *fs, va_list args)
{
  int err = errno;              /* must be first line of function */

  const char *name = threadlocal_get_name ();
  size_t instance_num = threadlocal_get_instance_num ();
  int tty;

  flockfile (stderr);
  tty = isatty (fileno (stderr));
  if (tty) fputs ("\033[1;31m", stderr);

  fprintf (stderr, "%s: ", program_name);

  if (name) {
    fprintf (stderr, "%s", name);
    if (instance_num > 0)
      fprintf (stderr, "[%zu]", instance_num);
    fprintf (stderr, ": ");
  }

  fprintf (stderr, "error: ");
  errno = err;                  /* must restore in case fs contains %m */
  vfprintf (stderr, fs, args);
  fprintf (stderr, "\n");

  if (tty) fputs ("\033[0m", stderr);

  funlockfile (stderr);

  errno = err;                  /* must be last line of function */
}
