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
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "utils.h"
#include "windows-compat.h"

#include "log.h"

/* We use the equivalent of printf ("") several times in this file
 * which worries GCC.  Ignore these.
 */
#pragma GCC diagnostic ignored "-Wformat-zero-length"

uint64_t connections;
const char *logfilename;
FILE *logfile;
const char *logscript;
int append;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pid_t saved_pid;

static void
log_unload (void)
{
  if (logfile)
    fclose (logfile);
}

/* Called for each key=value passed on the command line. */
static int
log_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
            const char *key, const char *value)
{
  if (strcmp (key, "logfile") == 0) {
    logfilename = value;
    return 0;
  }
  if (strcmp (key, "logappend") == 0) {
    append = nbdkit_parse_bool (value);
    if (append < 0)
      return -1;
    return 0;
  }
  if (strcmp (key, "logscript") == 0) {
    logscript = value;
    return 0;
  }
  return next (nxdata, key, value);
}

#define log_config_help \
  "logfile=<FILE>               The file to place the log in.\n" \
  "logappend=<BOOL>             True to append to the log (default false).\n" \
  "logscript=<SCRIPT>           Script to run for logging."

/* Open the logfile. */
static int
log_get_ready (int thread_model)
{
  int fd;

  if (logfilename) {
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
  }

  saved_pid = getpid ();

  print (NULL, "Ready", "thread_model=%d", thread_model);
  return 0;
}

static int
log_after_fork (nbdkit_backend *nxdata)
{
  /* Only issue this message if we actually fork. */
  if (getpid () != saved_pid)
    print (NULL, "Fork", "");

  return 0;
}

/* List exports. */
static int
log_list_exports (nbdkit_next_list_exports *next, nbdkit_backend *nxdata,
                  int readonly, int is_tls,
                  struct nbdkit_exports *exports)
{
  static log_id_t id;
  int r;
  int err;

  enter (NULL, ++id, "ListExports", "readonly=%d tls=%d", readonly, is_tls);
  r = next (nxdata, readonly, exports);
  if (r == -1) {
    err = errno;
    leave_simple (NULL, id, "ListExports", r, &err);
  }
  else {
    FILE *fp;
    CLEANUP_FREE char *str = NULL;
    size_t i, n, len = 0;

    fp = open_memstream (&str, &len);
    if (fp != NULL) {
      fprintf (fp, "exports=(");
      n = nbdkit_exports_count (exports);
      for (i = 0; i < n; ++i) {
        struct nbdkit_export e = nbdkit_get_export (exports, i);
        if (i > 0)
          fprintf (fp, " ");
        shell_quote (e.name, fp);
      }
      fprintf (fp, ") return=0");
      fclose (fp);
      leave (NULL, id, "ListExports", "%s", str);
    }
    else
      leave (NULL, id, "ListExports", "");
  }
  return r;
}

static int
log_preconnect (nbdkit_next_preconnect *next, nbdkit_backend *nxdata,
                int readonly)
{
  static log_id_t id;
  int r;
  int err;

  enter (NULL, ++id, "Preconnect", "readonly=%d", readonly);
  r = next (nxdata, readonly);
  if (r == -1)
    err = errno;
  leave_simple (NULL, id, "Preconnect", r, &err);
  return r;
}

/* Open a connection. */
static void *
log_open (nbdkit_next_open *next, nbdkit_context *nxdata,
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
  h->exportname = nbdkit_strdup_intern (exportname);
  if (h->exportname == NULL) {
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
  free (handle);
}

static int
log_prepare (nbdkit_next *next, void *handle,
             int readonly)
{
  FILE *fp;
  CLEANUP_FREE char *str = NULL;
  size_t len = 0;
  struct handle *h = handle;
  const char *exportname = h->exportname;
  int64_t size = next->get_size (next);
  uint32_t minsize, prefsize, maxsize;
  int w = next->can_write (next);
  int f = next->can_flush (next);
  int r = next->is_rotational (next);
  int t = next->can_trim (next);
  int z = next->can_zero (next);
  int F = next->can_fua (next);
  int e = next->can_extents (next);
  int c = next->can_cache (next);
  int Z = next->can_fast_zero (next);
  int s = next->block_size (next, &minsize, &prefsize, &maxsize);

  if (size < 0 || w < 0 || f < 0 || r < 0 || t < 0 || z < 0 || F < 0 ||
      e < 0 || c < 0 || Z < 0 || s < 0)
    return -1;

  fp = open_memstream (&str, &len);
  if (fp) {
    fprintf (fp, "export=");
    shell_quote (exportname, fp);
    fprintf (fp,
             " tls=%d size=0x%" PRIx64 " minsize=0x%" PRIx32 " prefsize=0x%"
             PRIx32 " maxsize=0x%" PRIx32 " write=%d "
             "flush=%d rotational=%d trim=%d zero=%d fua=%d extents=%d "
             "cache=%d fast_zero=%d",
             h->tls, size, minsize, prefsize, maxsize,
             w, f, r, t, z, F, e, c, Z);
    fclose (fp);
    print (h, "Connect", "%s", str);
  }
  else
    print (h, "Connect", "");

  return 0;
}

static int
log_finalize (nbdkit_next *next, void *handle)
{
  struct handle *h = handle;

  print (h, "Disconnect", "transactions=%" PRId64, h->id);
  return 0;
}

/* Read data. */
static int
log_pread (nbdkit_next *next,
           void *handle, void *buf, uint32_t count, uint64_t offs,
           uint32_t flags, int *err)
{
  struct handle *h = handle;
  int r;

  LOG (h, "Read", r, err, "offset=0x%" PRIx64 " count=0x%x", offs, count);

  assert (!flags);
  return r = next->pread (next, buf, count, offs, flags, err);
}

/* Write data. */
static int
log_pwrite (nbdkit_next *next,
            void *handle, const void *buf, uint32_t count, uint64_t offs,
            uint32_t flags, int *err)
{
  struct handle *h = handle;
  int r;

  LOG (h, "Write", r, err,
       "offset=0x%" PRIx64 " count=0x%x fua=%d",
       offs, count, !!(flags & NBDKIT_FLAG_FUA));

  assert (!(flags & ~NBDKIT_FLAG_FUA));
  return r = next->pwrite (next, buf, count, offs, flags, err);
}

/* Flush. */
static int
log_flush (nbdkit_next *next, void *handle,
           uint32_t flags, int *err)
{
  struct handle *h = handle;
  int r;

  LOG (h, "Flush", r, err, "");

  assert (!flags);
  return r = next->flush (next, flags, err);
}

/* Trim data. */
static int
log_trim (nbdkit_next *next,
          void *handle, uint32_t count, uint64_t offs, uint32_t flags,
          int *err)
{
  struct handle *h = handle;
  int r;

  LOG (h, "Trim", r, err,
       "offset=0x%" PRIx64 " count=0x%x fua=%d",
       offs, count, !!(flags & NBDKIT_FLAG_FUA));

  assert (!(flags & ~NBDKIT_FLAG_FUA));
  return r = next->trim (next, count, offs, flags, err);
}

/* Zero data. */
static int
log_zero (nbdkit_next *next,
          void *handle, uint32_t count, uint64_t offs, uint32_t flags,
          int *err)
{
  struct handle *h = handle;
  int r;

  LOG (h, "Zero", r, err,
       "offset=0x%" PRIx64 " count=0x%x trim=%d fua=%d fast=%d",
       offs, count, !!(flags & NBDKIT_FLAG_MAY_TRIM),
       !!(flags & NBDKIT_FLAG_FUA),
       !!(flags & NBDKIT_FLAG_FAST_ZERO));

  assert (!(flags & ~(NBDKIT_FLAG_FUA | NBDKIT_FLAG_MAY_TRIM |
                      NBDKIT_FLAG_FAST_ZERO)));
  return r = next->zero (next, count, offs, flags, err);
}

/* Extents. */
static int
log_extents (nbdkit_next *next,
             void *handle, uint32_t count, uint64_t offs, uint32_t flags,
             struct nbdkit_extents *extents, int *err)
{
  struct handle *h = handle;
  log_id_t id = get_id (h);
  int r;

  assert (!(flags & ~(NBDKIT_FLAG_REQ_ONE)));
  enter (h, id, "Extents",
         "offset=0x%" PRIx64 " count=0x%x req_one=%d",
         offs, count, !!(flags & NBDKIT_FLAG_REQ_ONE));
  r = next->extents (next, count, offs, flags, extents, err);
  if (r == -1)
    leave_simple (h, id, "Extents", r, err);
  else {
    FILE *fp;
    CLEANUP_FREE char *str = NULL;
    size_t i, n, len = 0;

    fp = open_memstream (&str, &len);
    if (fp != NULL) {
      fprintf (fp, "extents=(");
      n = nbdkit_extents_count (extents);
      for (i = 0; i < n; ++i) {
        bool comma = false;
        struct nbdkit_extent e = nbdkit_get_extent (extents, i);
        if (i > 0)
          fprintf (fp, " ");
        fprintf (fp, "0x%" PRIx64 " 0x%" PRIx64, e.offset, e.length);
        fprintf (fp, " \"");
        if ((e.type & NBDKIT_EXTENT_HOLE) != 0) {
          fprintf (fp, "hole");
          comma = true;
        }
        if ((e.type & NBDKIT_EXTENT_ZERO) != 0) {
          if (comma) fprintf (fp, ",");
          fprintf (fp, "zero");
        }
        fprintf (fp, "\"");
      }
      fprintf (fp, ") return=0");
      fclose (fp);
      leave (h, id, "Extents", "%s", str);
    }
    else
      leave (h, id, "Extents", "");
  }
  return r;
}

/* Cache data. */
static int
log_cache (nbdkit_next *next,
           void *handle, uint32_t count, uint64_t offs, uint32_t flags,
           int *err)
{
  struct handle *h = handle;
  int r;

  LOG (h, "Cache", r, err, "offset=0x%" PRIx64 " count=0x%x", offs, count);

  assert (!flags);
  return r = next->cache (next, count, offs, flags, err);
}

static struct nbdkit_filter filter = {
  .name              = "log",
  .longname          = "nbdkit log filter",
  .config            = log_config,
  .config_help       = log_config_help,
  .unload            = log_unload,
  .get_ready         = log_get_ready,
  .after_fork        = log_after_fork,
  .list_exports      = log_list_exports,
  .preconnect        = log_preconnect,
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

NBDKIT_REGISTER_FILTER (filter)
