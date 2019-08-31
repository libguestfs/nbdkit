/* nbdkit
 * Copyright (C) 2018-2019 Red Hat Inc.
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

#include <nbdkit-filter.h>

#include "minmax.h"

#define MAX_WRITE (64 * 1024 * 1024)

static enum ZeroMode {
  NONE,
  EMULATE,
  NOTRIM,
} zeromode;

static int
nozero_config (nbdkit_next_config *next, void *nxdata,
               const char *key, const char *value)
{
  if (strcmp (key, "zeromode") == 0) {
    if (strcmp (value, "emulate") == 0)
      zeromode = EMULATE;
    else if (strcmp (value, "notrim") == 0)
      zeromode = NOTRIM;
    else if (strcmp (value, "none") != 0) {
      nbdkit_error ("unknown zeromode '%s'", value);
      return -1;
    }
    return 0;
  }
  return next (nxdata, key, value);
}

#define nozero_config_help \
  "zeromode=<MODE>      Either 'none' (default), 'emulate', or 'notrim'.\n" \

/* Check that desired mode is supported by plugin. */
static int
nozero_prepare (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle,
                int readonly)
{
  int r;

  /* If we are opened readonly, this filter has no impact */
  if (readonly)
    return 0;

  if (zeromode == NOTRIM) {
    r = next_ops->can_zero (nxdata);
    if (r == -1)
      return -1;
    if (!r) {
      nbdkit_error ("zeromode 'notrim' requires plugin zero support");
      return -1;
    }
  }
  return 0;
}

/* Advertise desired WRITE_ZEROES mode. */
static int
nozero_can_zero (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  return zeromode != NONE;
}

static int
nozero_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle, uint32_t count, uint64_t offs, uint32_t flags,
             int *err)
{
  int writeflags = 0;
  bool need_flush = false;

  assert (zeromode != NONE);
  flags &= ~NBDKIT_FLAG_MAY_TRIM;

  if (zeromode == NOTRIM)
    return next_ops->zero (nxdata, count, offs, flags, err);

  if (flags & NBDKIT_FLAG_FUA) {
    if (next_ops->can_fua (nxdata) == NBDKIT_FUA_EMULATE)
      need_flush = true;
    else
      writeflags = NBDKIT_FLAG_FUA;
  }

  while (count) {
    /* Always contains zeroes, but we can't use const or else gcc 9
     * will use .rodata instead of .bss and inflate the binary size.
     */
    static /* const */ char buffer[MAX_WRITE];
    uint32_t size = MIN (count, MAX_WRITE);

    if (size == count && need_flush)
      writeflags = NBDKIT_FLAG_FUA;

    if (next_ops->pwrite (nxdata, buffer, size, offs, writeflags, err) == -1)
      return -1;
    offs += size;
    count -= size;
  }
  return 0;
}

static struct nbdkit_filter filter = {
  .name              = "nozero",
  .longname          = "nbdkit nozero filter",
  .config            = nozero_config,
  .config_help       = nozero_config_help,
  .prepare           = nozero_prepare,
  .can_zero          = nozero_can_zero,
  .zero              = nozero_zero,
};

NBDKIT_REGISTER_FILTER(filter)
