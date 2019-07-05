/* nbdkit
 * Copyright (C) 2018 Red Hat Inc.
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

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

#define MAX_WRITE (64 * 1024 * 1024)

static bool emulate;

static int
nozero_config (nbdkit_next_config *next, void *nxdata,
               const char *key, const char *value)
{
  if (strcmp (key, "zeromode") == 0) {
    if (strcmp (value, "emulate") == 0)
      emulate = true;
    else if (strcmp (value, "none") != 0) {
      nbdkit_error ("unknown zeromode '%s'", value);
      return -1;
    }
    return 0;
  }
  return next (nxdata, key, value);
}

#define nozero_config_help \
  "zeromode=<MODE>      Either 'none' (default) or 'emulate'.\n" \

/* Advertise desired WRITE_ZEROES mode. */
static int
nozero_can_zero (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  return emulate;
}

static int
nozero_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle, uint32_t count, uint64_t offs, uint32_t flags,
             int *err)
{
  assert (emulate);
  while (count) {
    /* Always contains zeroes, but we can't use const or else gcc 9
     * will use .rodata instead of .bss and inflate the binary size.
     */
    static /* const */ char buffer[MAX_WRITE];
    uint32_t size = MIN (count, MAX_WRITE);
    if (next_ops->pwrite (nxdata, buffer, size, offs,
                          flags & ~NBDKIT_FLAG_MAY_TRIM, err) == -1)
      return -1;
    offs += size;
    count -= size;
  }
  return 0;
}

static struct nbdkit_filter filter = {
  .name              = "nozero",
  .longname          = "nbdkit nozero filter",
  .version           = PACKAGE_VERSION,
  .config            = nozero_config,
  .config_help       = nozero_config_help,
  .can_zero          = nozero_can_zero,
  .zero              = nozero_zero,
};

NBDKIT_REGISTER_FILTER(filter)
