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
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include <nbdkit-filter.h>

#include "minmax.h"

static enum CacheMode {
  NONE,
  EMULATE,
  NOP,
} cachemode;

static int
nocache_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
                const char *key, const char *value)
{
  if (strcmp (key, "cachemode") == 0) {
    if (strcmp (value, "emulate") == 0)
      cachemode = EMULATE;
    else if (strcmp (value, "nop") == 0 ||
             strcmp (value, "no-op") == 0)
      cachemode = NOP;
    else if (strcmp (value, "none") != 0) {
      nbdkit_error ("unknown cachemode '%s'", value);
      return -1;
    }
    return 0;
  }
  return next (nxdata, key, value);
}

#define nocache_config_help \
  "cachemode=<MODE>     Either 'none' (default), 'emulate', or 'nop'.\n" \

/* Advertise desired FLAG_SEND_CACHE mode. */
static int
nocache_can_cache (nbdkit_next *next,
                   void *handle)
{
  switch (cachemode) {
  case NONE:
    return NBDKIT_CACHE_NONE;
  case EMULATE:
    return NBDKIT_CACHE_EMULATE;
  case NOP:
    return NBDKIT_CACHE_NATIVE;
  }
  abort ();
}

static int
nocache_cache (nbdkit_next *next,
               void *handle, uint32_t count, uint64_t offs, uint32_t flags,
               int *err)
{
  assert (cachemode == NOP);
  assert (!flags);

  return 0;
}

static struct nbdkit_filter filter = {
  .name              = "nocache",
  .longname          = "nbdkit nocache filter",
  .config            = nocache_config,
  .config_help       = nocache_config_help,
  .can_cache         = nocache_can_cache,
  .cache             = nocache_cache,
};

NBDKIT_REGISTER_FILTER (filter)
