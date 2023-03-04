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
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/time.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "utils.h"
#include "windows-compat.h"

#include "log.h"

enum type { ENTER, LEAVE, PRINT };

/* Adds an entry to the logfile. */
static void
to_file (struct handle *h, log_id_t id, const char *act, enum type type,
         const char *fmt, va_list args)
{
  struct timeval tv;
  struct tm tm;
  char timestamp[27] = "Time unknown";

  /* Logging is best effort, so ignore failure to get timestamp */
  if (!gettimeofday (&tv, NULL)) {
    size_t s;

    gmtime_r (&tv.tv_sec, &tm);
    s = strftime (timestamp, sizeof timestamp - sizeof ".000000" + 1,
                  "%F %T", &tm);
    assert (s);
    snprintf (timestamp + s, sizeof timestamp - s, ".%06ld",
              0L + tv.tv_usec);
  }

#ifdef HAVE_FLOCKFILE
  flockfile (logfile);
#endif

  if (h)
    fprintf (logfile, "%s connection=%" PRIu64 " %s%s",
             timestamp, h->connection, type == LEAVE ? "..." : "", act);
  else
    fprintf (logfile, "%s %s%s",
             timestamp, type == LEAVE ? "..." : "",act);

  if (id)
    fprintf (logfile, " id=%" PRIu64, id);

  if (fmt[0] != 0)
    fprintf (logfile, " ");
  vfprintf (logfile, fmt, args);

  if (type == ENTER)
    fprintf (logfile, " ...");

  fputc ('\n', logfile);
  fflush (logfile);
#ifdef HAVE_FUNLOCKFILE
  funlockfile (logfile);
#endif
}

/* Runs the script. */
static void
to_script (struct handle *h, log_id_t id, const char *act, enum type type,
           const char *fmt, va_list args)
{
  FILE *fp;
  CLEANUP_FREE char *str = NULL;
  size_t len = 0;
  int r;

  /* Create the shell variables + script. */
  fp = open_memstream (&str, &len);
  if (!fp) {
    /* Not much we can do, but at least record the error. */
    nbdkit_error ("logscript: open_memstream: %m");
    return;
  }

  fprintf (fp, "act=%s\n", act);
  if (h)
    fprintf (fp, "connection=%" PRIu64 "\n", h->connection);
  switch (type) {
  case ENTER: fprintf (fp, "type=ENTER\n"); break;
  case LEAVE: fprintf (fp, "type=LEAVE\n"); break;
  case PRINT: fprintf (fp, "type=PRINT\n"); break;
  }
  if (id)
    fprintf (fp, "id=%" PRIu64 "\n", id);

  vfprintf (fp, fmt, args);
  fprintf (fp, "\n");

  fprintf (fp, "%s", logscript);
  fclose (fp);

  /* Run the script.  Log the status, but ignore it. */
  r = system (str);
  exit_status_to_nbd_error (r, "logscript");
}

void
enter (struct handle *h, log_id_t id, const char *act,
       const char *fmt, ...)
{
  va_list args;

  if (logfile) {
    va_start (args, fmt);
    to_file (h, id, act, ENTER, fmt, args);
    va_end (args);
  }
  if (logscript) {
    va_start (args, fmt);
    to_script (h, id, act, ENTER, fmt, args);
    va_end (args);
  }
}

void
leave (struct handle *h, log_id_t id, const char *act,
       const char *fmt, ...)
{
  va_list args;

  if (logfile) {
    va_start (args, fmt);
    to_file (h, id, act, LEAVE, fmt, args);
    va_end (args);
  }
  if (logscript) {
    va_start (args, fmt);
    to_script (h, id, act, LEAVE, fmt, args);
    va_end (args);
  }
}

void
print (struct handle *h, const char *act, const char *fmt, ...)
{
  va_list args;

  if (logfile) {
    va_start (args, fmt);
    to_file (h, 0, act, PRINT, fmt, args);
    va_end (args);
  }
  if (logscript) {
    va_start (args, fmt);
    to_script (h, 0, act, PRINT, fmt, args);
    va_end (args);
  }
}

void
leave_simple (struct handle *h, log_id_t id, const char *act, int r, int *err)
{
  const char *s;

  /* Only decode what server/protocol.c:nbd_errno() recognizes */
  if (r == -1) {
    switch (*err) {
    case EROFS:
    case EPERM:
      s = " error=EPERM";
      break;
    case EIO:
      s = " error=EIO";
      break;
    case ENOMEM:
      s = " error=ENOMEM";
      break;
#ifdef EDQUOT
    case EDQUOT:
#endif
    case EFBIG:
    case ENOSPC:
      s = " error=ENOSPC";
      break;
#ifdef ESHUTDOWN
    case ESHUTDOWN:
      s = " error=ESHUTDOWN";
      break;
#endif
    case ENOTSUP:
#if ENOTSUP != EOPNOTSUPP
    case EOPNOTSUPP:
#endif
      s = " error=ENOTSUP";
      break;
    case EOVERFLOW:
      s = " error=EOVERFLOW";
      break;
    case EINVAL:
    default:
      s = " error=EINVAL";
    }
  }
  else
    s = "";

  leave (h, id, act, "return=%d%s", r, s);
}

void
leave_simple2 (struct leave_simple_params *params)
{
  leave_simple (params->h, params->id, params->act, *params->r, params->err);
}
