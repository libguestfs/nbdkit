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
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <sys/time.h>

#include <pthread.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "tvdiff.h"

static char *filename;
static bool append;
static FILE *fp;
static struct timeval start_t;

/* This lock protects all the stats. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t pread_ops, pread_bytes;
static uint64_t pwrite_ops, pwrite_bytes;
static uint64_t trim_ops, trim_bytes;
static uint64_t zero_ops, zero_bytes;
static uint64_t extents_ops, extents_bytes;
static uint64_t cache_ops, cache_bytes;

static inline double
calc_bps (uint64_t bytes, int64_t usecs)
{
  return 8.0 * bytes / usecs * 1000000.;
}

static inline void
print_stats (int64_t usecs)
{
  fprintf (fp, "elapsed time: %g s\n", usecs / 1000000.);

  if (pread_ops > 0)
    fprintf (fp, "read: %" PRIu64 " ops, %" PRIu64 " bytes, %g bits/s\n",
             pread_ops, pread_bytes, calc_bps (pread_bytes, usecs));
  if (pwrite_ops > 0)
    fprintf (fp, "write: %" PRIu64 " ops, %" PRIu64 " bytes, %g bits/s\n",
             pwrite_ops, pwrite_bytes, calc_bps (pwrite_bytes, usecs));
  if (trim_ops > 0)
    fprintf (fp, "trim: %" PRIu64 " ops, %" PRIu64 " bytes, %g bits/s\n",
             trim_ops, trim_bytes, calc_bps (trim_bytes, usecs));
  if (zero_ops > 0)
    fprintf (fp, "zero: %" PRIu64 " ops, %" PRIu64 " bytes, %g bits/s\n",
             zero_ops, zero_bytes, calc_bps (zero_bytes, usecs));
  if (extents_ops > 0)
    fprintf (fp, "extents: %" PRIu64 " ops, %" PRIu64 " bytes, %g bits/s\n",
             extents_ops, extents_bytes, calc_bps (extents_bytes, usecs));
  if (cache_ops > 0)
    fprintf (fp, "cache: %" PRIu64 " ops, %" PRIu64 " bytes, %g bits/s\n",
             cache_ops, cache_bytes, calc_bps (cache_bytes, usecs));

  fflush (fp);
}

static void
stats_unload (void)
{
  struct timeval now;
  int64_t usecs;

  gettimeofday (&now, NULL);
  usecs = tvdiff_usec (&start_t, &now);
  if (fp && usecs > 0) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    print_stats (usecs);
  }

  if (fp)
    fclose (fp);
  free (filename);
}

static int
stats_config (nbdkit_next_config *next, void *nxdata,
              const char *key, const char *value)
{
  int r;

  if (strcmp (key, "statsfile") == 0) {
    free (filename);
    filename = nbdkit_absolute_path (value);
    if (filename == NULL)
      return -1;
    return 0;
  }
  else if (strcmp (value, "statsappend") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    append = r;
    return 0;
  }

  return next (nxdata, key, value);
}

static int
stats_config_complete (nbdkit_next_config_complete *next, void *nxdata)
{
  if (filename == NULL) {
    nbdkit_error ("stats filter requires statsfile parameter");
    return -1;
  }

  fp = fopen (filename, append ? "a" : "w");
  if (fp == NULL) {
    nbdkit_error ("%s: %m", filename);
    return -1;
  }

  gettimeofday (&start_t, NULL);

  return next (nxdata);
}

/* Read. */
static int
stats_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle, void *buf, uint32_t count, uint64_t offset,
             uint32_t flags, int *err)
{
  int r;

  r = next_ops->pread (nxdata, buf, count, offset, flags, err);
  if (r == 0) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    pread_ops++;
    pread_bytes += count;
  }
  return r;
}

/* Write. */
static int
stats_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
              void *handle,
              const void *buf, uint32_t count, uint64_t offset,
              uint32_t flags, int *err)
{
  int r;

  r = next_ops->pwrite (nxdata, buf, count, offset, flags, err);
  if (r == 0) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    pwrite_ops++;
    pwrite_bytes += count;
  }
  return r;
}

/* Trim. */
static int
stats_trim (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle,
            uint32_t count, uint64_t offset, uint32_t flags,
            int *err)
{
  int r;

  r = next_ops->trim (nxdata, count, offset, flags, err);
  if (r == 0) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    trim_ops++;
    trim_bytes += count;
  }
  return r;
}

/* Zero. */
static int
stats_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle,
            uint32_t count, uint64_t offset, uint32_t flags,
            int *err)
{
  int r;

  r = next_ops->zero (nxdata, count, offset, flags, err);
  if (r == 0) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    zero_ops++;
    zero_bytes += count;
  }
  return r;
}

/* Extents. */
static int
stats_extents (struct nbdkit_next_ops *next_ops, void *nxdata,
               void *handle,
               uint32_t count, uint64_t offset, uint32_t flags,
               struct nbdkit_extents *extents, int *err)
{
  int r;

  r = next_ops->extents (nxdata, count, offset, flags, extents, err);
  if (r == 0) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    extents_ops++;
    /* XXX There's a case for trying to determine how long the extents
     * will be that are returned to the client, given the flags and
     * the complex rules in the protocol.
     */
    extents_bytes += count;
  }
  return r;
}

/* Cache. */
static int
stats_cache (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle,
             uint32_t count, uint64_t offset, uint32_t flags,
             int *err)
{
  int r;

  r = next_ops->cache (nxdata, count, offset, flags, err);
  if (r == 0) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    cache_ops++;
    cache_bytes += count;
  }
  return r;
}

static struct nbdkit_filter filter = {
  .name              = "stats",
  .longname          = "nbdkit stats filter",
  .version           = PACKAGE_VERSION,
  .unload            = stats_unload,
  .config            = stats_config,
  .config_complete   = stats_config_complete,
  .pread             = stats_pread,
  .pwrite            = stats_pwrite,
  .trim              = stats_trim,
  .zero              = stats_zero,
  .extents           = stats_extents,
  .cache             = stats_cache,
};

NBDKIT_REGISTER_FILTER(filter)
