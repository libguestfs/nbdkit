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
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "internal.h"

/* Call the right log_*_verror function depending on log_sink.
 * Note: preserves the previous value of errno.
 */
void
log_verror (const char *fs, va_list args)
{
  switch (log_to) {
  case LOG_TO_DEFAULT:
    if (forked_into_background)
      log_syslog_verror (fs, args);
    else
      log_stderr_verror (fs, args);
    break;
  case LOG_TO_SYSLOG:
    log_syslog_verror (fs, args);
    break;
  case LOG_TO_STDERR:
    log_stderr_verror (fs, args);
    break;
  case LOG_TO_NULL:
    /* nothing */
    break;
  }
}

/* Note: preserves the previous value of errno. */
void
nbdkit_verror (const char *fs, va_list args)
{
  log_verror (fs, args);
}

/* Note: preserves the previous value of errno. */
void
nbdkit_error (const char *fs, ...)
{
  va_list args;

  va_start (args, fs);
  nbdkit_verror (fs, args);
  va_end (args);
}
