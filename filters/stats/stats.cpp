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

#include <unordered_map>
#include <vector>
#include <algorithm>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>

#include <pthread.h>

#include <nbdkit-filter.h>

#include "cleanup.h"

#include "tvdiff.h"
#include "windows-compat.h"

static char *filename;
static bool append;
static FILE *fp;
static struct timeval start_t;

typedef struct {
  const char *name;
  uint64_t ops;
  uint64_t bytes;
  uint64_t usecs;
} nbdstat;

typedef std::unordered_map<size_t, size_t> blksize_hist_t;

/* This lock protects all the stats. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static nbdstat pread_st   = { "read" };
static nbdstat pwrite_st  = { "write" };
static nbdstat trim_st    = { "trim" };
static nbdstat zero_st    = { "zero" };
static nbdstat extents_st = { "extents" };
static nbdstat cache_st   = { "cache" };
static nbdstat flush_st   = { "flush" };
static blksize_hist_t blksize_pread_st;
static blksize_hist_t blksize_pwrite_st;
static blksize_hist_t blksize_trim_st;
static blksize_hist_t blksize_zero_st;

#define KiB 1024
#define MiB 1048576
#define GiB 1073741824

static char *
humansize (uint64_t bytes)
{
  int r;
  char *ret;

  if (bytes < KiB)
    r = asprintf (&ret, "%" PRIu64 " bytes", bytes);
  else if (bytes < MiB)
    r = asprintf (&ret, "%.2f KiB", bytes / (double)KiB);
  else if (bytes < GiB)
    r = asprintf (&ret, "%.2f MiB", bytes / (double)MiB);
  else
    r = asprintf (&ret, "%.2f GiB", bytes / (double)GiB);
  if (r == -1)
    ret = NULL;
  return ret;
}

static char *
humanrate (uint64_t bytes, uint64_t usecs)
{
  double secs = usecs / 1000000.0;
  return secs != 0.0 ? humansize (bytes / secs) : NULL;
}

static inline const char *
maybe (char *s)
{
  return s ? s : "(n/a)";
}

static void
print_stat (const nbdstat *st, int64_t usecs)
{
  if (st->ops > 0) {
    char *size = humansize (st->bytes);
    char *op_rate = humanrate (st->bytes, st->usecs);
    char *total_rate = humanrate (st->bytes, usecs);

    fprintf (fp, "%s: %" PRIu64 " ops, %.6f s, %s, %s/s op, %s/s total\n",
             st->name, st->ops, st->usecs / 1000000.0, maybe (size),
             maybe (op_rate), maybe (total_rate));

    free (size);
    free (op_rate);
    free (total_rate);
  }
}

static void
print_totals (uint64_t usecs)
{
  uint64_t ops = pread_st.ops + pwrite_st.ops + trim_st.ops + zero_st.ops +
    extents_st.ops + flush_st.ops;
  uint64_t bytes = pread_st.bytes + pwrite_st.bytes + trim_st.bytes +
    zero_st.bytes;
  char *size = humansize (bytes);
  char *rate = humanrate (bytes, usecs);

  fprintf (fp, "total: %" PRIu64 " ops, %.6f s, %s, %s/s\n",
           ops, usecs / 1000000.0, maybe (size), maybe (rate));

  free (size);
  free (rate);
}

static void
print_histogram (const blksize_hist_t hist, int count)
{
  double total = 0;
  for (auto el : hist) {
    total += static_cast<double> (el.second);
  }

  // Sort
  auto pairs = std::vector<std::pair<size_t, size_t>> (hist.begin(), hist.end());
  std::sort(pairs.begin(), pairs.end(),
    [](decltype(pairs[0]) a, decltype(pairs[0]) b) {
      return a.second > b.second;
    });

  int i = 0;
  for (auto el : pairs) {
    if (++i >= count)
      break;
    fprintf (fp, "%13zu         %9zu (%.2f%%)\n",
      el.first, el.second, static_cast<double>(el.second) / total * 100);
  }
}

static void
print_blocksize_stats (void)
{
  fprintf (fp, "\nREAD Request sizes (top 28):\n");
  fprintf (fp, "    blocksize     request count\n");
  print_histogram (blksize_pread_st, 28);
  
  fprintf (fp, "\nWRITE Request sizes (top 28):\n");
  fprintf (fp, "    blocksize     request count\n");
  print_histogram (blksize_pwrite_st, 28);

  fprintf (fp, "\nTRIM Request sizes (top 28):\n");
  fprintf (fp, "    blocksize     request count\n");
  print_histogram (blksize_trim_st, 28);
  
  fprintf (fp, "\nZERO Request sizes (top 28):\n");
  fprintf (fp, "    blocksize     request count\n");
  print_histogram (blksize_zero_st, 28);
}

static inline void
print_stats (int64_t usecs)
{
  print_totals (usecs);
  print_stat (&pread_st,   usecs);
  print_stat (&pwrite_st,  usecs);
  print_stat (&trim_st,    usecs);
  print_stat (&zero_st,    usecs);
  print_stat (&extents_st, usecs);
  print_stat (&cache_st,   usecs);
  print_stat (&flush_st,   usecs);
  print_blocksize_stats();
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
stats_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
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
  else if (strcmp (key, "statsappend") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    append = r;
    return 0;
  }

  return next (nxdata, key, value);
}

static int
stats_config_complete (nbdkit_next_config_complete *next,
                       nbdkit_backend *nxdata)
{
  if (filename == NULL) {
    nbdkit_error ("stats filter requires statsfile parameter");
    return -1;
  }

  return next (nxdata);
}

static int
stats_get_ready (int thread_model)
{
  int fd;

  /* Using fopen("ae"/"we") would be more convenient, but as Haiku
   * still lacks that, use this instead. Atomicity is not essential
   * here since .config completes before threads that might fork, if
   * we have to later add yet another fallback to fcntl(fileno()) for
   * systems without O_CLOEXEC.
   */
  fd = open (filename,
             O_CLOEXEC | O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC),
             0666);
  if (fd < 0) {
    nbdkit_error ("open: %s: %m", filename);
    return -1;
  }
  fp = fdopen (fd, append ? "a" : "w");
  if (fp == NULL) {
    nbdkit_error ("fdopen: %s: %m", filename);
    return -1;
  }

  gettimeofday (&start_t, NULL);

  return 0;
}

#define stats_config_help \
  "statsfile=<FILE>    (required) The file to place the log in.\n" \
  "statsappend=<BOOL>  True to append to the log (default false).\n"

static inline void
record_stat (nbdstat *st, uint32_t count, const struct timeval *start)
{
  struct timeval end;
  uint64_t usecs;

  gettimeofday (&end, NULL);
  usecs = tvdiff_usec (start, &end);

  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
  st->ops++;
  st->bytes += count;
  st->usecs += usecs;
}

/* Read. */
static int
stats_pread (nbdkit_next *next,
             void *handle, void *buf, uint32_t count, uint64_t offset,
             uint32_t flags, int *err)
{
  struct timeval start;
  int r;

  {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    blksize_pread_st[count]++;
  }

  gettimeofday (&start, NULL);
  r = next->pread (next, buf, count, offset, flags, err);
  if (r == 0) record_stat (&pread_st, count, &start);
  return r;
}

/* Write. */
static int
stats_pwrite (nbdkit_next *next,
              void *handle,
              const void *buf, uint32_t count, uint64_t offset,
              uint32_t flags, int *err)
{
  struct timeval start;
  int r;

  {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    blksize_pwrite_st[count]++;
  }
  
  gettimeofday (&start, NULL);
  r = next->pwrite (next, buf, count, offset, flags, err);
  if (r == 0) record_stat (&pwrite_st, count, &start);
  return r;
}

/* Trim. */
static int
stats_trim (nbdkit_next *next,
            void *handle,
            uint32_t count, uint64_t offset, uint32_t flags,
            int *err)
{
  struct timeval start;
  int r;

  {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    blksize_trim_st[count]++;
  }

  gettimeofday (&start, NULL);
  r = next->trim (next, count, offset, flags, err);
  if (r == 0) record_stat (&trim_st, count, &start);
  return r;
}

/* Flush. */
static int
stats_flush (nbdkit_next *next,
             void *handle, uint32_t flags,
             int *err)
{
  struct timeval start;
  int r;

  gettimeofday (&start, NULL);
  r = next->flush (next, flags, err);
  if (r == 0) record_stat (&flush_st, 0, &start);
  return r;
}

/* Zero. */
static int
stats_zero (nbdkit_next *next,
            void *handle,
            uint32_t count, uint64_t offset, uint32_t flags,
            int *err)
{
  struct timeval start;
  int r;

  {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    blksize_zero_st[count]++;
  }

  gettimeofday (&start, NULL);
  r = next->zero (next, count, offset, flags, err);
  if (r == 0) record_stat (&zero_st, count, &start);
  return r;
}

/* Extents. */
static int
stats_extents (nbdkit_next *next,
               void *handle,
               uint32_t count, uint64_t offset, uint32_t flags,
               struct nbdkit_extents *extents, int *err)
{
  struct timeval start;
  int r;

  gettimeofday (&start, NULL);
  r = next->extents (next, count, offset, flags, extents, err);
  /* XXX There's a case for trying to determine how long the extents
   * will be that are returned to the client (instead of simply using
   * count), given the flags and the complex rules in the protocol.
   */
  if (r == 0) record_stat (&extents_st, count, &start);
  return r;
}

/* Cache. */
static int
stats_cache (nbdkit_next *next,
             void *handle,
             uint32_t count, uint64_t offset, uint32_t flags,
             int *err)
{
  struct timeval start;
  int r;

  gettimeofday (&start, NULL);
  r = next->cache (next, count, offset, flags, err);
  if (r == 0) record_stat (&cache_st, count, &start);
  return r;
}

static struct nbdkit_filter filter = []() -> nbdkit_filter {
	auto f = nbdkit_filter();
  f.name = "stats";
  f.longname = "nbdkit stats filter";
  f.unload = stats_unload;
  f.config = stats_config;
  f.config_complete = stats_config_complete;
  f.config_help = stats_config_help;
  f.get_ready = stats_get_ready;
  f.pread = stats_pread;
  f.pwrite = stats_pwrite;
  f.flush = stats_flush;
  f.trim = stats_trim;
  f.zero = stats_zero;
  f.extents = stats_extents;
  f.cache = stats_cache;

    return f;
}();

NBDKIT_REGISTER_FILTER(filter)
