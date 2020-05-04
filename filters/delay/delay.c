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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include <nbdkit-filter.h>

static int delay_read_ms = 0;   /* read delay (milliseconds) */
static int delay_write_ms = 0;  /* write delay (milliseconds) */
static int delay_zero_ms = 0;   /* zero delay (milliseconds) */
static int delay_trim_ms = 0;   /* trim delay (milliseconds) */
static int delay_extents_ms = 0;/* extents delay (milliseconds) */
static int delay_cache_ms = 0;  /* cache delay (milliseconds) */
static int delay_fast_zero = 1; /* whether delaying zero includes fast zero */

static int
parse_delay (const char *key, const char *value)
{
  size_t len = strlen (value);
  int r;

  if (len > 2 && strcmp (&value[len-2], "ms") == 0) {
    if (sscanf (value, "%d", &r) == 1 && r >= 0)
      return r;
    else {
      nbdkit_error ("cannot parse %s in milliseconds parameter: %s",
                    key, value);
      return -1;
    }
  }
  else {
    if (sscanf (value, "%d", &r) == 1 && r >= 0) {
      if (r * 1000LL > INT_MAX) {
        nbdkit_error ("seconds parameter %s is too large: %s",
                      key, value);
        return -1;
      }
      return r * 1000;
    }
    else {
      nbdkit_error ("cannot parse %s in seconds parameter: %s",
                    key, value);
      return -1;
    }
  }
}

static int
delay (int ms, int *err)
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

/* Called for each key=value passed on the command line. */
static int
delay_config (nbdkit_next_config *next, void *nxdata,
              const char *key, const char *value)
{
  if (strcmp (key, "rdelay") == 0 ||
      strcmp (key, "delay-read") == 0 ||
      strcmp (key, "delay-reads") == 0) {
    delay_read_ms = parse_delay (key, value);
    if (delay_read_ms == -1)
      return -1;
    return 0;
  }
  else if (strcmp (key, "wdelay") == 0) {
    delay_write_ms = parse_delay (key, value);
    if (delay_write_ms == -1)
      return -1;
    /* Historically wdelay set all write-related delays. */
    delay_zero_ms = delay_trim_ms = delay_write_ms;
    return 0;
  }
  else if (strcmp (key, "delay-write") == 0 ||
           strcmp (key, "delay-writes") == 0) {
    delay_write_ms = parse_delay (key, value);
    if (delay_write_ms == -1)
      return -1;
    return 0;
  }
  else if (strcmp (key, "delay-zero") == 0 ||
           strcmp (key, "delay-zeroes") == 0) {
    delay_zero_ms = parse_delay (key, value);
    if (delay_zero_ms == -1)
      return -1;
    return 0;
  }
  else if (strcmp (key, "delay-trim") == 0 ||
           strcmp (key, "delay-trims") == 0 ||
           strcmp (key, "delay-discard") == 0 ||
           strcmp (key, "delay-discards") == 0) {
    delay_trim_ms = parse_delay (key, value);
    if (delay_trim_ms == -1)
      return -1;
    return 0;
  }
  else if (strcmp (key, "delay-extent") == 0 ||
           strcmp (key, "delay-extents") == 0) {
    delay_extents_ms = parse_delay (key, value);
    if (delay_extents_ms == -1)
      return -1;
    return 0;
  }
  else if (strcmp (key, "delay-cache") == 0) {
    delay_cache_ms = parse_delay (key, value);
    if (delay_cache_ms == -1)
      return -1;
    return 0;
  }
  else if (strcmp (key, "delay-fast-zero") == 0) {
    delay_fast_zero = nbdkit_parse_bool (value);
    if (delay_fast_zero < 0)
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
  "delay-fast-zero=<BOOL>         Delay fast zero requests (default true).\n"

/* Override the plugin's .can_fast_zero if needed */
static int
delay_can_fast_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
                     void *handle)
{
  /* Advertise if we are handling fast zero requests locally */
  if (delay_zero_ms && !delay_fast_zero)
    return 1;
  return next_ops->can_fast_zero (nxdata);
}

/* Read data. */
static int
delay_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle, void *buf, uint32_t count, uint64_t offset,
             uint32_t flags, int *err)
{
  if (read_delay (err) == -1)
    return -1;
  return next_ops->pread (nxdata, buf, count, offset, flags, err);
}

/* Write data. */
static int
delay_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
              void *handle,
              const void *buf, uint32_t count, uint64_t offset, uint32_t flags,
              int *err)
{
  if (write_delay (err) == -1)
    return -1;
  return next_ops->pwrite (nxdata, buf, count, offset, flags, err);
}

/* Zero data. */
static int
delay_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle, uint32_t count, uint64_t offset, uint32_t flags,
            int *err)
{
  if ((flags & NBDKIT_FLAG_FAST_ZERO) && delay_zero_ms && !delay_fast_zero) {
    *err = ENOTSUP;
    return -1;
  }
  if (zero_delay (err) == -1)
    return -1;
  return next_ops->zero (nxdata, count, offset, flags, err);
}

/* Trim data. */
static int
delay_trim (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle, uint32_t count, uint64_t offset,
            uint32_t flags, int *err)
{
  if (trim_delay (err) == -1)
    return -1;
  return next_ops->trim (nxdata, count, offset, flags, err);
}

/* Extents. */
static int
delay_extents (struct nbdkit_next_ops *next_ops, void *nxdata,
               void *handle, uint32_t count, uint64_t offset, uint32_t flags,
               struct nbdkit_extents *extents, int *err)
{
  if (extents_delay (err) == -1)
    return -1;
  return next_ops->extents (nxdata, count, offset, flags, extents, err);
}

/* Cache. */
static int
delay_cache (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle, uint32_t count, uint64_t offset, uint32_t flags,
             int *err)
{
  if (cache_delay (err) == -1)
    return -1;
  return next_ops->cache (nxdata, count, offset, flags, err);
}

static struct nbdkit_filter filter = {
  .name              = "delay",
  .longname          = "nbdkit delay filter",
  .config            = delay_config,
  .config_help       = delay_config_help,
  .can_fast_zero     = delay_can_fast_zero,
  .pread             = delay_pread,
  .pwrite            = delay_pwrite,
  .zero              = delay_zero,
  .trim              = delay_trim,
  .extents           = delay_extents,
  .cache             = delay_cache,
};

NBDKIT_REGISTER_FILTER(filter)
