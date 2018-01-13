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
#include <time.h>

#include <nbdkit-filter.h>

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

static int rdelayms = 0;        /* read delay (milliseconds) */
static int wdelayms = 0;        /* write delay (milliseconds) */

static int
parse_delay (const char *value)
{
  size_t len = strlen (value);
  int r;

  if (len > 2 && strcmp (&value[len-2], "ms") == 0) {
    if (sscanf (value, "%d", &r) == 1)
      return r;
    else {
      nbdkit_error ("cannot parse rdelay/wdelay milliseconds parameter: %s",
                    value);
      return -1;
    }
  }
  else {
    if (sscanf (value, "%d", &r) == 1)
      return r * 1000;
    else {
      nbdkit_error ("cannot parse rdelay/wdelay seconds parameter: %s",
                    value);
      return -1;
    }
  }
}

static void
delay (int ms)
{
  if (ms > 0) {
    const struct timespec ts = {
      .tv_sec = ms / 1000,
      .tv_nsec = (ms * 1000000) % 1000000000
    };
    nanosleep (&ts, NULL);
  }
}

static void
read_delay (void)
{
  delay (rdelayms);
}

static void
write_delay (void)
{
  delay (wdelayms);
}

/* Called for each key=value passed on the command line. */
static int
delay_config (nbdkit_next_config *next, void *nxdata,
              const char *key, const char *value)
{
  if (strcmp (key, "rdelay") == 0) {
    rdelayms = parse_delay (value);
    if (rdelayms == -1)
      return -1;
    return 0;
  }
  else if (strcmp (key, "wdelay") == 0) {
    wdelayms = parse_delay (value);
    if (wdelayms == -1)
      return -1;
    return 0;
  }
  else
    return next (nxdata, key, value);
}

#define delay_config_help \
  "rdelay=<NN>[ms]                Read delay in seconds/milliseconds.\n" \
  "wdelay=<NN>[ms]                Write delay in seconds/milliseconds." \

/* Read data. */
static int
delay_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle, void *buf, uint32_t count, uint64_t offset)
{
  read_delay ();
  return next_ops->pread (nxdata, buf, count, offset);
}

/* Write data. */
static int
delay_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
              void *handle,
              const void *buf, uint32_t count, uint64_t offset)
{
  write_delay ();
  return next_ops->pwrite (nxdata, buf, count, offset);
}

/* Zero data. */
static int
delay_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle, uint32_t count, uint64_t offset, int may_trim)
{
  write_delay ();
  return next_ops->zero (nxdata, count, offset, may_trim);
}

static struct nbdkit_filter filter = {
  .name              = "delay",
  .longname          = "nbdkit delay filter",
  .version           = PACKAGE_VERSION,
  .config            = delay_config,
  .config_help       = delay_config_help,
  .pread             = delay_pread,
  .pwrite            = delay_pwrite,
  .zero              = delay_zero,
};

NBDKIT_REGISTER_FILTER(filter)
