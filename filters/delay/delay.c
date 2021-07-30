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
#include <stdbool.h>
#include <errno.h>
#include <limits.h>

#include <nbdkit-filter.h>

static unsigned delay_read_ms = 0;   /* read delay (milliseconds) */
static unsigned delay_write_ms = 0;  /* write delay (milliseconds) */
static unsigned delay_zero_ms = 0;   /* zero delay (milliseconds) */
static unsigned delay_trim_ms = 0;   /* trim delay (milliseconds) */
static unsigned delay_extents_ms = 0;/* extents delay (milliseconds) */
static unsigned delay_cache_ms = 0;  /* cache delay (milliseconds) */
static unsigned delay_open_ms = 0;   /* open delay (milliseconds) */
static unsigned delay_close_ms = 0;  /* close delay (milliseconds) */

static int delay_fast_zero = 1; /* whether delaying zero includes fast zero */

static int
parse_delay (const char *key, const char *value, unsigned *r)
{
  size_t len = strlen (value);

  if (len > 2 && strcmp (&value[len-2], "ms") == 0) {
    /* We have to use sscanf here instead of nbdkit_parse_unsigned
     * because that function will reject the "ms" suffix.
     */
    if (sscanf (value, "%u", r) == 1)
      return 0;
    else {
      nbdkit_error ("cannot parse %s in milliseconds parameter: %s",
                    key, value);
      return -1;
    }
  }
  else {
    if (nbdkit_parse_unsigned (key, value, r) == -1)
      return -1;
    if (*r * 1000U > UINT_MAX) {
      nbdkit_error ("seconds parameter %s is too large: %s", key, value);
      return -1;
    }
    *r *= 1000;
    return 0;
  }
}

static int
delay (unsigned ms, int *err)
{
  if (ms > 0 && nbdkit_nanosleep (ms / 1000, (ms % 1000) * 1000000) == -1) {
    *err = errno;
    return -1;
  }
  return 0;
}

static int
read_delay (int *err)
{
  return delay (delay_read_ms, err);
}

static int
write_delay (int *err)
{
  return delay (delay_write_ms, err);
}

static int
zero_delay (int *err)
{
  return delay (delay_zero_ms, err);
}

static int
trim_delay (int *err)
{
  return delay (delay_trim_ms, err);
}

static int
extents_delay (int *err)
{
  return delay (delay_extents_ms, err);
}

static int
cache_delay (int *err)
{
  return delay (delay_cache_ms, err);
}

static int
open_delay (int *err)
{
  return delay (delay_open_ms, err);
}

static int
close_delay (int *err)
{
  return delay (delay_close_ms, err);
}

/* Called for each key=value passed on the command line. */
static int
delay_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
              const char *key, const char *value)
{
  if (strcmp (key, "rdelay") == 0 ||
      strcmp (key, "delay-read") == 0 ||
      strcmp (key, "delay-reads") == 0) {
    if (parse_delay (key, value, &delay_read_ms) == -1)
      return -1;
    return 0;
  }
  else if (strcmp (key, "wdelay") == 0) {
    if (parse_delay (key, value, &delay_write_ms) == -1)
      return -1;
    /* Historically wdelay set all write-related delays. */
    delay_zero_ms = delay_trim_ms = delay_write_ms;
    return 0;
  }
  else if (strcmp (key, "delay-write") == 0 ||
           strcmp (key, "delay-writes") == 0) {
    if (parse_delay (key, value, &delay_write_ms) == -1)
      return -1;
    return 0;
  }
  else if (strcmp (key, "delay-zero") == 0 ||
           strcmp (key, "delay-zeroes") == 0) {
    if (parse_delay (key, value, &delay_zero_ms) == -1)
      return -1;
    return 0;
  }
  else if (strcmp (key, "delay-trim") == 0 ||
           strcmp (key, "delay-trims") == 0 ||
           strcmp (key, "delay-discard") == 0 ||
           strcmp (key, "delay-discards") == 0) {
    if (parse_delay (key, value, &delay_trim_ms) == -1)
      return -1;
    return 0;
  }
  else if (strcmp (key, "delay-extent") == 0 ||
           strcmp (key, "delay-extents") == 0) {
    if (parse_delay (key, value, &delay_extents_ms) == -1)
      return -1;
    return 0;
  }
  else if (strcmp (key, "delay-cache") == 0) {
    if (parse_delay (key, value, &delay_cache_ms) == -1)
      return -1;
    return 0;
  }
  else if (strcmp (key, "delay-fast-zero") == 0) {
    delay_fast_zero = nbdkit_parse_bool (value);
    if (delay_fast_zero < 0)
      return -1;
    return 0;
  }
  else if (strcmp (key, "delay-open") == 0) {
    if (parse_delay (key, value, &delay_open_ms) == -1)
      return -1;
    return 0;
  }
  else if (strcmp (key, "delay-close") == 0) {
    if (parse_delay (key, value, &delay_close_ms) == -1)
      return -1;
    return 0;
  }
  else
    return next (nxdata, key, value);
}

#define delay_config_help \
  "rdelay=<NN>[ms]                Read delay in seconds/milliseconds.\n" \
  "delay-read=<NN>[ms]            Read delay in seconds/milliseconds.\n" \
  "delay-write=<NN>[ms]           Write delay in seconds/milliseconds.\n" \
  "delay-zero=<NN>[ms]            Zero delay in seconds/milliseconds.\n" \
  "delay-trim=<NN>[ms]            Trim delay in seconds/milliseconds.\n" \
  "delay-extents=<NN>[ms]         Extents delay in seconds/milliseconds.\n" \
  "delay-cache=<NN>[ms]           Cache delay in seconds/milliseconds.\n" \
  "wdelay=<NN>[ms]                Write, zero and trim delay in secs/msecs.\n" \
  "delay-fast-zero=<BOOL>         Delay fast zero requests (default true).\n" \
  "delay-open=<NN>[ms]            Open delay in seconds/milliseconds.\n" \
  "delay-close=<NN>[ms]           Close delay in seconds/milliseconds."

/* Override the plugin's .can_fast_zero if needed */
static int
delay_can_fast_zero (nbdkit_next *next,
                     void *handle)
{
  /* Advertise if we are handling fast zero requests locally */
  if (delay_zero_ms && !delay_fast_zero)
    return 1;
  return next->can_fast_zero (next);
}

/* Open connection. */
static void *
delay_open (nbdkit_next_open *next, nbdkit_context *nxdata,
            int readonly, const char *exportname, int is_tls)
{
  int err;

  if (open_delay (&err) == -1) {
    errno = err;
    nbdkit_error ("delay: %m");
    return NULL;
  }

  if (next (nxdata, readonly, exportname) == -1)
    return NULL;

  return NBDKIT_HANDLE_NOT_NEEDED;
}

/* Close connection. */
static void
delay_close (void *handle)
{
  int err;

  close_delay (&err);
}

/* Read data. */
static int
delay_pread (nbdkit_next *next,
             void *handle, void *buf, uint32_t count, uint64_t offset,
             uint32_t flags, int *err)
{
  if (read_delay (err) == -1)
    return -1;
  return next->pread (next, buf, count, offset, flags, err);
}

/* Write data. */
static int
delay_pwrite (nbdkit_next *next,
              void *handle,
              const void *buf, uint32_t count, uint64_t offset, uint32_t flags,
              int *err)
{
  if (write_delay (err) == -1)
    return -1;
  return next->pwrite (next, buf, count, offset, flags, err);
}

/* Zero data. */
static int
delay_zero (nbdkit_next *next,
            void *handle, uint32_t count, uint64_t offset, uint32_t flags,
            int *err)
{
  if ((flags & NBDKIT_FLAG_FAST_ZERO) && delay_zero_ms && !delay_fast_zero) {
    *err = ENOTSUP;
    return -1;
  }
  if (zero_delay (err) == -1)
    return -1;
  return next->zero (next, count, offset, flags, err);
}

/* Trim data. */
static int
delay_trim (nbdkit_next *next,
            void *handle, uint32_t count, uint64_t offset,
            uint32_t flags, int *err)
{
  if (trim_delay (err) == -1)
    return -1;
  return next->trim (next, count, offset, flags, err);
}

/* Extents. */
static int
delay_extents (nbdkit_next *next,
               void *handle, uint32_t count, uint64_t offset, uint32_t flags,
               struct nbdkit_extents *extents, int *err)
{
  if (extents_delay (err) == -1)
    return -1;
  return next->extents (next, count, offset, flags, extents, err);
}

/* Cache. */
static int
delay_cache (nbdkit_next *next,
             void *handle, uint32_t count, uint64_t offset, uint32_t flags,
             int *err)
{
  if (cache_delay (err) == -1)
    return -1;
  return next->cache (next, count, offset, flags, err);
}

static struct nbdkit_filter filter = {
  .name              = "delay",
  .longname          = "nbdkit delay filter",
  .config            = delay_config,
  .config_help       = delay_config_help,
  .can_fast_zero     = delay_can_fast_zero,
  .open              = delay_open,
  .close             = delay_close,
  .pread             = delay_pread,
  .pwrite            = delay_pwrite,
  .zero              = delay_zero,
  .trim              = delay_trim,
  .extents           = delay_extents,
  .cache             = delay_cache,
};

NBDKIT_REGISTER_FILTER(filter)
