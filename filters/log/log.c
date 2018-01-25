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
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/time.h>
#include <assert.h>

#include <nbdkit-filter.h>

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

static uint64_t connections;
static char *logfilename;
static FILE *logfile;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void
log_unload (void)
{
  if (logfilename)
    fclose (logfile);
  free (logfilename);
}

/* Called for each key=value passed on the command line. */
static int
log_config (nbdkit_next_config *next, void *nxdata,
            const char *key, const char *value)
{
  if (strcmp (key, "logfile") == 0) {
    free (logfilename);
    logfilename = strdup (value);
    if (!logfilename) {
      nbdkit_error ("strdup: %m");
      return -1;
    }
    return 0;
  }
  return next (nxdata, key, value);
}

/* Open the logfile. */
static int
log_config_complete (nbdkit_next_config_complete *next, void *nxdata)
{
  if (!logfilename) {
    nbdkit_error ("missing logfile= parameter for the log filter");
    return -1;
  }
  logfile = fopen (logfilename, "w");
  if (!logfile) {
    nbdkit_error ("fopen: %m");
    return -1;
  }

  return next (nxdata);
}

#define log_config_help \
  "logfile=<FILE>    The file to place the log in."

struct handle {
  uint64_t connection;
  uint64_t id;
};

/* Compute the next id number on the current connection. */
static uint64_t
get_id (struct handle *h)
{
  uint64_t r;

  pthread_mutex_lock (&lock);
  r = ++h->id;
  pthread_mutex_unlock (&lock);
  return r;
}

/* Output a timestamp and the log message. */
static void __attribute__ ((format (printf, 4, 5)))
output (struct handle *h, const char *act, uint64_t id, const char *fmt, ...)
{
  va_list args;
  struct timeval tv;
  struct tm tm;
  char timestamp[27] = "Time unknown";

  /* Logging is best effort, so ignore failure to get timestamp */
  if (!gettimeofday (&tv, NULL))
    {
      size_t s;

      gmtime_r (&tv.tv_sec, &tm);
      s = strftime (timestamp, sizeof timestamp, "%F %T", &tm);
      assert (s);
      s = snprintf (timestamp + s, sizeof timestamp - s, ".%06ld",
                    0L + tv.tv_usec);
    }
  flockfile (logfile);
  fprintf (logfile, "%s connection=%" PRIu64 " %s ", timestamp, h->connection,
           act);
  if (id)
    fprintf (logfile, "id=%" PRIu64 " ", id);
  va_start (args, fmt);
  vfprintf (logfile, fmt, args);
  va_end (args);
  fputc ('\n', logfile);
  fflush (logfile);
  funlockfile (logfile);
}

/* Shared code for a nicer log of return value */
static void
output_return (struct handle *h, const char *act, uint64_t id, int r, int *err)
{
  const char *s = "Other=>EINVAL";

  /* Only decode what connections.c:nbd_errno() recognizes */
  if (r == -1) {
    switch (*err) {
    case EROFS:
      s = "EROFS=>EPERM";
      break;
    case EPERM:
      s = "EPERM";
      break;
    case EIO:
      s = "EIO";
      break;
    case ENOMEM:
      s = "ENOMEM";
      break;
#ifdef EDQUOT
    case EDQUOT:
      s = "EDQUOT=>ENOSPC";
      break;
#endif
    case EFBIG:
      s = "EFBIG=>ENOSPC";
      break;
    case ENOSPC:
      s = "ENOSPC";
      break;
#ifdef ESHUTDOWN
    case ESHUTDOWN:
      s = "ESHUTDOWN";
      break;
#endif
    case EINVAL:
      s = "EINVAL";
      break;
    }
  }
  else {
    s = "Success";
  }
  output (h, act, id, "return=%d (%s)", r, s);
}

/* Open a connection. */
static void *
log_open (nbdkit_next_open *next, void *nxdata, int readonly)
{
  struct handle *h;

  if (next (nxdata, readonly) == -1)
    return NULL;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  pthread_mutex_lock (&lock);
  h->connection = ++connections;
  pthread_mutex_unlock (&lock);
  h->id = 0;
  return h;
}

static void
log_close (void *handle)
{
  struct handle *h = handle;

  free (h);
}

static int
log_prepare (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  struct handle *h = handle;
  int64_t size = next_ops->get_size (nxdata);
  int w = next_ops->can_write (nxdata);
  int f = next_ops->can_flush (nxdata);
  int r = next_ops->is_rotational (nxdata);
  int t = next_ops->can_trim (nxdata);
  int z = next_ops->can_zero (nxdata);
  int F = next_ops->can_fua (nxdata);

  if (size < 0 || w < 0 || f < 0 || r < 0 || t < 0 || z < 0 || F < 0)
    return -1;

  output (h, "Connect", 0, "size=0x%" PRIx64 " write=%d flush=%d "
          "rotational=%d trim=%d zero=%d fua=%d", size, w, f, r, t, z, F);
  return 0;
}

static int
log_finalize (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  struct handle *h = handle;

  output (h, "Disconnect", 0, "transactions=%" PRId64, h->id);
  return 0;
}

/* Read data. */
static int
log_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
           void *handle, void *buf, uint32_t count, uint64_t offs,
           uint32_t flags, int *err)
{
  struct handle *h = handle;
  uint64_t id = get_id (h);
  int r;

  assert (!flags);
  output (h, "Read", id, "offset=0x%" PRIx64 " count=0x%x ...",
          offs, count);
  r = next_ops->pread (nxdata, buf, count, offs, flags, err);
  output_return (h, "...Read", id, r, err);
  return r;
}

/* Write data. */
static int
log_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle, const void *buf, uint32_t count, uint64_t offs,
            uint32_t flags, int *err)
{
  struct handle *h = handle;
  uint64_t id = get_id (h);
  int r;

  assert (!(flags & ~NBDKIT_FLAG_FUA));
  output (h, "Write", id, "offset=0x%" PRIx64 " count=0x%x fua=%d ...",
          offs, count, !!(flags & NBDKIT_FLAG_FUA));
  r = next_ops->pwrite (nxdata, buf, count, offs, flags, err);
  output_return (h, "...Write", id, r, err);
  return r;
}

/* Flush. */
static int
log_flush (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle,
           uint32_t flags, int *err)
{
  struct handle *h = handle;
  uint64_t id = get_id (h);
  int r;

  assert (!flags);
  output (h, "Flush", id, "...");
  r = next_ops->flush (nxdata, flags, err);
  output_return (h, "...Flush", id, r, err);
  return r;
}

/* Trim data. */
static int
log_trim (struct nbdkit_next_ops *next_ops, void *nxdata,
          void *handle, uint32_t count, uint64_t offs, uint32_t flags,
          int *err)
{
  struct handle *h = handle;
  uint64_t id = get_id (h);
  int r;

  assert (!(flags & ~NBDKIT_FLAG_FUA));
  output (h, "Trim", id, "offset=0x%" PRIx64 " count=0x%x fua=%d ...",
          offs, count, !!(flags & NBDKIT_FLAG_FUA));
  r = next_ops->trim (nxdata, count, offs, flags, err);
  output_return (h, "...Trim", id, r, err);
  return r;
}

/* Zero data. */
static int
log_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
          void *handle, uint32_t count, uint64_t offs, uint32_t flags,
          int *err)
{
  struct handle *h = handle;
  uint64_t id = get_id (h);
  int r;

  assert (!(flags & ~(NBDKIT_FLAG_FUA | NBDKIT_FLAG_MAY_TRIM)));
  output (h, "Zero", id, "offset=0x%" PRIx64 " count=0x%x trim=%d fua=%d ...",
          offs, count, !!(flags & NBDKIT_FLAG_MAY_TRIM),
          !!(flags & NBDKIT_FLAG_FUA));
  r = next_ops->zero (nxdata, count, offs, flags, err);
  output_return (h, "...Zero", id, r, err);
  return r;
}

static struct nbdkit_filter filter = {
  .name              = "log",
  .longname          = "nbdkit log filter",
  .version           = PACKAGE_VERSION,
  .config            = log_config,
  .config_complete   = log_config_complete,
  .config_help       = log_config_help,
  .unload            = log_unload,
  .open              = log_open,
  .close             = log_close,
  .prepare           = log_prepare,
  .finalize          = log_finalize,
  .pread             = log_pread,
  .pwrite            = log_pwrite,
  .flush             = log_flush,
  .trim              = log_trim,
  .zero              = log_zero,
};

NBDKIT_REGISTER_FILTER(filter)
