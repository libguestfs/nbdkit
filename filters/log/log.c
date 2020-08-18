/* nbdkit
 * Copyright (C) 2018-2020 Red Hat Inc.
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

static uint64_t connections;
static char *logfilename;
static FILE *logfile;
static int append;
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
  if (strcmp (key, "logappend") == 0) {
    append = nbdkit_parse_bool (value);
    if (append < 0)
      return -1;
    return 0;
  }
  return next (nxdata, key, value);
}

static int
log_config_complete (nbdkit_next_config_complete *next, void *nxdata)
{
  if (!logfilename) {
    nbdkit_error ("missing logfile= parameter for the log filter");
    return -1;
  }

  return next (nxdata);
}

/* Open the logfile. */
static int
log_get_ready (nbdkit_next_get_ready *next, void *nxdata, int thread_model)
{
  int fd;

  /* Using fopen("ae"/"we") would be more convenient, but as Haiku
   * still lacks that, use this instead. Atomicity is not essential
   * here since .config completes before threads that might fork, if
   * we have to later add yet another fallback to fcntl(fileno()) for
   * systems without O_CLOEXEC.
   */
  fd = open (logfilename,
             O_CLOEXEC | O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC),
             0666);
  if (fd < 0) {
    nbdkit_error ("open: %s: %m", logfilename);
    return -1;
  }
  logfile = fdopen (fd, append ? "a" : "w");
  if (!logfile) {
    nbdkit_error ("fdopen: %s: %m", logfilename);
    close (fd);
    return -1;
  }

  return next (nxdata);
}

#define log_config_help \
  "logfile=<FILE>    (required) The file to place the log in.\n" \
  "logappend=<BOOL>  True to append to the log (default false).\n"

struct handle {
  uint64_t connection;
  uint64_t id;
  char *exportname;
  int tls;
};

/* Compute the next id number on the current connection. */
static uint64_t
get_id (struct handle *h)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE(&lock);
  return ++h->id;
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
    fprintf (logfile, "%s connection=%" PRIu64 " %s ", timestamp,
             h->connection, act);
  else
    fprintf (logfile, "%s %s ", timestamp, act);
  if (id)
    fprintf (logfile, "id=%" PRIu64 " ", id);
  va_start (args, fmt);
  vfprintf (logfile, fmt, args);
  va_end (args);
  fputc ('\n', logfile);
  fflush (logfile);
#ifdef HAVE_FUNLOCKFILE
  funlockfile (logfile);
#endif
}

/* Shared code for a nicer log of return value */
static void
output_return (struct handle *h, const char *act, uint64_t id, int r, int *err)
{
  const char *s = "Other=>EINVAL";

  /* Only decode what protocol.c:nbd_errno() recognizes */
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
    case ENOTSUP:
#if ENOTSUP != EOPNOTSUPP
    case EOPNOTSUPP:
#endif
      s = "ENOTSUP";
      break;
    case EOVERFLOW:
      s = "EOVERFLOW";
      break;
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

/* List exports. */
static int
log_list_exports (nbdkit_next_list_exports *next, void *nxdata,
                  int readonly, int default_only,
                  struct nbdkit_exports *exports)
{
  static uint64_t id;
  int r;
  int err;

  output (NULL, "ListExports", ++id, "readonly=%d default_only=%d ...",
          readonly, default_only);
  r = next (nxdata, readonly, default_only, exports);
  if (r == -1) {
    err = errno;
    output_return (NULL, "...ListExports", id, r, &err);
  }
  else {
    FILE *fp;
    CLEANUP_FREE char *exports_str = NULL;
    size_t i, n, len = 0;

    fp = open_memstream (&exports_str, &len);
    if (fp != NULL) {
      n = nbdkit_exports_count (exports);
      for (i = 0; i < n; ++i) {
        struct nbdkit_export e = nbdkit_get_export (exports, i);
        if (i > 0)
          fprintf (fp, ", ");
        shell_quote (e.name, fp);
      }

      fclose (fp);
    }

    output (NULL, "...ListExports", id, "exports=[%s] return=0",
            exports_str ? exports_str : "(null)");
  }
  return r;
}

/* Open a connection. */
static void *
log_open (nbdkit_next_open *next, void *nxdata,
          int readonly, const char *exportname, int is_tls)
{
  struct handle *h;

  if (next (nxdata, readonly, exportname) == -1)
    return NULL;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  /* Save the exportname and tls state in the handle so we can display
   * it in log_prepare.  We must take a copy because this string has a
   * short lifetime.
   */
  h->exportname = strdup (exportname);
  if (h->exportname == NULL) {
    nbdkit_error ("strdup: %m");
    free (h);
    return NULL;
  }
  h->tls = is_tls;

  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
  h->connection = ++connections;
  h->id = 0;
  return h;
}

static void
log_close (void *handle)
{
  struct handle *h = handle;

  free (h->exportname);
  free (h);
}

static int
log_prepare (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle,
             int readonly)
{
  struct handle *h = handle;
  const char *exportname = h->exportname;
  int64_t size = next_ops->get_size (nxdata);
  int w = next_ops->can_write (nxdata);
  int f = next_ops->can_flush (nxdata);
  int r = next_ops->is_rotational (nxdata);
  int t = next_ops->can_trim (nxdata);
  int z = next_ops->can_zero (nxdata);
  int F = next_ops->can_fua (nxdata);
  int e = next_ops->can_extents (nxdata);
  int c = next_ops->can_cache (nxdata);
  int Z = next_ops->can_fast_zero (nxdata);

  if (size < 0 || w < 0 || f < 0 || r < 0 || t < 0 || z < 0 || F < 0 ||
      e < 0 || c < 0 || Z < 0)
    return -1;

  output (h, "Connect", 0, "export='%s' tls=%d size=0x%" PRIx64 " write=%d "
          "flush=%d rotational=%d trim=%d zero=%d fua=%d extents=%d cache=%d "
          "fast_zero=%d", exportname, h->tls, size, w, f, r, t, z, F, e, c, Z);
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

  assert (!(flags & ~(NBDKIT_FLAG_FUA | NBDKIT_FLAG_MAY_TRIM |
                      NBDKIT_FLAG_FAST_ZERO)));
  output (h, "Zero", id,
          "offset=0x%" PRIx64 " count=0x%x trim=%d fua=%d fast=%d...",
          offs, count, !!(flags & NBDKIT_FLAG_MAY_TRIM),
          !!(flags & NBDKIT_FLAG_FUA),
          !!(flags & NBDKIT_FLAG_FAST_ZERO));
  r = next_ops->zero (nxdata, count, offs, flags, err);
  output_return (h, "...Zero", id, r, err);
  return r;
}

/* Extents. */
static int
log_extents (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle, uint32_t count, uint64_t offs, uint32_t flags,
             struct nbdkit_extents *extents, int *err)
{
  struct handle *h = handle;
  uint64_t id = get_id (h);
  int r;

  assert (!(flags & ~(NBDKIT_FLAG_REQ_ONE)));
  output (h, "Extents", id,
          "offset=0x%" PRIx64 " count=0x%x req_one=%d ...",
          offs, count, !!(flags & NBDKIT_FLAG_REQ_ONE));
  r = next_ops->extents (nxdata, count, offs, flags, extents, err);
  if (r == -1)
    output_return (h, "...Extents", id, r, err);
  else {
    FILE *fp;
    CLEANUP_FREE char *extents_str = NULL;
    size_t i, n, len = 0;

    fp = open_memstream (&extents_str, &len);
    if (fp != NULL) {
      n = nbdkit_extents_count (extents);
      for (i = 0; i < n; ++i) {
        struct nbdkit_extent e = nbdkit_get_extent (extents, i);
        if (i > 0)
          fprintf (fp, ", ");
        fprintf (fp, "{ offset=0x%" PRIx64 ", length=0x%" PRIx64 ", "
                 "hole=%d, zero=%d }",
                 e.offset, e.length,
                 !!(e.type & NBDKIT_EXTENT_HOLE),
                 !!(e.type & NBDKIT_EXTENT_ZERO));
      }

      fclose (fp);
    }

    output (h, "...Extents", id, "extents=[%s] return=0",
            extents_str ? extents_str : "(null)");
  }
  return r;
}

/* Cache data. */
static int
log_cache (struct nbdkit_next_ops *next_ops, void *nxdata,
           void *handle, uint32_t count, uint64_t offs, uint32_t flags,
           int *err)
{
  struct handle *h = handle;
  uint64_t id = get_id (h);
  int r;

  assert (!flags);
  output (h, "Cache", id, "offset=0x%" PRIx64 " count=0x%x ...",
          offs, count);
  r = next_ops->cache (nxdata, count, offs, flags, err);
  output_return (h, "...Cache", id, r, err);
  return r;
}

static struct nbdkit_filter filter = {
  .name              = "log",
  .longname          = "nbdkit log filter",
  .config            = log_config,
  .config_complete   = log_config_complete,
  .config_help       = log_config_help,
  .unload            = log_unload,
  .get_ready         = log_get_ready,
  .list_exports      = log_list_exports,
  .open              = log_open,
  .close             = log_close,
  .prepare           = log_prepare,
  .finalize          = log_finalize,
  .pread             = log_pread,
  .pwrite            = log_pwrite,
  .flush             = log_flush,
  .trim              = log_trim,
  .zero              = log_zero,
  .extents           = log_extents,
  .cache             = log_cache,
};

NBDKIT_REGISTER_FILTER(filter)
