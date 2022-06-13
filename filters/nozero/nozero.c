/* nbdkit
 * Copyright (C) 2018-2022 Red Hat Inc.
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
#include <assert.h>
#include <errno.h>

#include <nbdkit-filter.h>

#include "minmax.h"

/* IGNORE is defined as a macro in Windows headers files ... */
#ifdef IGNORE
#undef IGNORE
#endif

static enum ZeroMode {
  NONE,
  EMULATE,
  NOTRIM,
  PLUGIN,
} zeromode;

static enum FastZeroMode {
  DEFAULT,
  SLOW,
  IGNORE,
  NOFAST,
} fastzeromode;

static int
nozero_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
               const char *key, const char *value)
{
  if (strcmp (key, "zeromode") == 0) {
    if (strcmp (value, "emulate") == 0)
      zeromode = EMULATE;
    else if (strcmp (value, "notrim") == 0)
      zeromode = NOTRIM;
    else if (strcmp (value, "plugin") == 0)
      zeromode = PLUGIN;
    else if (strcmp (value, "none") != 0) {
      nbdkit_error ("unknown zeromode '%s'", value);
      return -1;
    }
    return 0;
  }

  if (strcmp (key, "fastzeromode") == 0) {
    if (strcmp (value, "none") == 0)
      fastzeromode = NOFAST;
    else if (strcmp (value, "ignore") == 0)
      fastzeromode = IGNORE;
    else if (strcmp (value, "slow") == 0)
      fastzeromode = SLOW;
    else if (strcmp (value, "default") != 0) {
      nbdkit_error ("unknown fastzeromode '%s'", value);
      return -1;
    }
    return 0;
  }

  return next (nxdata, key, value);
}

#define nozero_config_help \
  "zeromode=<MODE>      One of 'none' (default), 'emulate', 'notrim', 'plugin'.\n" \
  "fastzeromode=<MODE>  One of 'default', 'none', 'slow', 'ignore'.\n"

/* Check that desired mode is supported by plugin. */
static int
nozero_prepare (nbdkit_next *next, void *handle,
                int readonly)
{
  int r;

  /* If we are opened readonly, this filter has no impact */
  if (readonly)
    return 0;

  if (zeromode == NOTRIM || zeromode == PLUGIN) {
    r = next->can_zero (next);
    if (r == -1)
      return -1;
    if (!r) {
      nbdkit_error ("zeromode '%s' requires plugin zero support",
                    zeromode == NOTRIM ? "notrim" : "plugin");
      return -1;
    }
  }
  return 0;
}

/* Advertise desired WRITE_ZEROES mode. */
static int
nozero_can_zero (nbdkit_next *next, void *handle)
{
  switch (zeromode) {
  case NONE:
    return NBDKIT_ZERO_NONE;
  case EMULATE:
    return NBDKIT_ZERO_EMULATE;
  default:
    return next->can_zero (next);
  }
}

/* Advertise desired FAST_ZERO mode. */
static int
nozero_can_fast_zero (nbdkit_next *next,
                      void *handle)
{
  if (zeromode == NONE)
    return 0;
  if (zeromode != EMULATE && fastzeromode == DEFAULT)
    return next->can_fast_zero (next);
  return fastzeromode != NOFAST;
}

static int
nozero_zero (nbdkit_next *next,
             void *handle, uint32_t count, uint64_t offs, uint32_t flags,
             int *err)
{
  assert (zeromode != NONE && zeromode != EMULATE);
  if (flags & NBDKIT_FLAG_FAST_ZERO) {
    assert (fastzeromode != NOFAST);
    if (fastzeromode == SLOW) {
      *err = ENOTSUP;
      return -1;
    }
    if (fastzeromode == IGNORE)
      flags &= ~NBDKIT_FLAG_FAST_ZERO;
  }

  if (zeromode == NOTRIM)
    flags &= ~NBDKIT_FLAG_MAY_TRIM;

  return next->zero (next, count, offs, flags, err);
}

static struct nbdkit_filter filter = {
  .name              = "nozero",
  .longname          = "nbdkit nozero filter",
  .config            = nozero_config,
  .config_help       = nozero_config_help,
  .prepare           = nozero_prepare,
  .can_zero          = nozero_can_zero,
  .can_fast_zero     = nozero_can_fast_zero,
  .zero              = nozero_zero,
};

NBDKIT_REGISTER_FILTER(filter)
