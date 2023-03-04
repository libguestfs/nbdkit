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

static enum FuaMode {
  NONE,
  EMULATE,
  NATIVE,
  FORCE,
  PASS,
  DISCARD,
} fuamode;

static int
fua_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
            const char *key, const char *value)
{
  if (strcmp (key, "fuamode") == 0) {
    if (strcmp (value, "none") == 0)
      fuamode = NONE;
    if (strcmp (value, "emulate") == 0)
      fuamode = EMULATE;
    else if (strcmp (value, "native") == 0)
      fuamode = NATIVE;
    else if (strcmp (value, "force") == 0)
      fuamode = FORCE;
    else if (strcmp (value, "pass") == 0)
      fuamode = PASS;
    else if (strcmp (value, "discard") == 0)
      fuamode = DISCARD;
    else {
      nbdkit_error ("unknown fuamode '%s'", value);
      return -1;
    }
    return 0;
  }
  return next (nxdata, key, value);
}

#define fua_config_help \
  "fuamode=<MODE>       One of 'none' (default), 'emulate', 'native',\n" \
  "                       'force', 'pass'."

/* Check that desired mode is supported by plugin. */
static int
fua_prepare (nbdkit_next *next, void *handle,
             int readonly)
{
  int r;

  /* If we are opened readonly, this filter has no impact */
  if (readonly)
    return 0;

  switch (fuamode) {
  case NONE:
  case PASS:
  case DISCARD:
    break;
  case EMULATE:
    r = next->can_flush (next);
    if (r == -1)
      return -1;
    if (r == 0) {
      nbdkit_error ("fuamode 'emulate' requires plugin flush support");
      return -1;
    }
    break;
  case NATIVE:
  case FORCE:
    r = next->can_fua (next);
    if (r == -1)
      return -1;
    if (r == NBDKIT_FUA_NONE) {
      nbdkit_error ("fuamode '%s' requires plugin fua support",
                    fuamode == EMULATE ? "emulate" : "force");
      return -1;
    }
    break;
  }
  return 0;
}

/* Advertise proper flush support. */
static int
fua_can_flush (nbdkit_next *next, void *handle)
{
  switch (fuamode) {
  case FORCE:
  case DISCARD:
    return 1; /* Advertise our no-op flush, even if plugin lacks it */
  case NONE:
  case EMULATE:
  case NATIVE:
  case PASS:
    return next->can_flush (next);
  }
  abort ();
}

/* Advertise desired fua mode. */
static int
fua_can_fua (nbdkit_next *next, void *handle)
{
  switch (fuamode) {
  case NONE:
    return NBDKIT_FUA_NONE;
  case EMULATE:
    return NBDKIT_FUA_EMULATE;
  case NATIVE:
  case FORCE:
  case DISCARD:
    return NBDKIT_FUA_NATIVE;
  case PASS:
    return next->can_fua (next);
  }
  abort ();
}

static int
fua_pwrite (nbdkit_next *next,
            void *handle, const void *buf, uint32_t count, uint64_t offs,
            uint32_t flags, int *err)
{
  int r;
  bool need_flush = false;

  switch (fuamode) {
  case NONE:
    assert (!(flags & NBDKIT_FLAG_FUA));
    break;
  case EMULATE:
    if (flags & NBDKIT_FLAG_FUA) {
      need_flush = true;
      flags &= ~NBDKIT_FLAG_FUA;
    }
    break;
  case NATIVE:
  case PASS:
    break;
  case FORCE:
    flags |= NBDKIT_FLAG_FUA;
    break;
  case DISCARD:
    flags &= ~NBDKIT_FLAG_FUA;
    break;
  }
  r = next->pwrite (next, buf, count, offs, flags, err);
  if (r != -1 && need_flush)
    r = next->flush (next, 0, err);
  return r;
}

static int
fua_flush (nbdkit_next *next,
           void *handle, uint32_t flags, int *err)
{
  switch (fuamode) {
  case FORCE:
    return 0; /* Nothing to flush, since all writes already used FUA */
  case DISCARD:
    return 0; /* Drop flushes! */
  case NONE:
  case EMULATE:
  case NATIVE:
  case PASS:
    return next->flush (next, flags, err);
  }
  abort ();
}

static int
fua_trim (nbdkit_next *next,
          void *handle, uint32_t count, uint64_t offs, uint32_t flags,
          int *err)
{
  int r;
  bool need_flush = false;

  switch (fuamode) {
  case NONE:
    assert (!(flags & NBDKIT_FLAG_FUA));
    break;
  case EMULATE:
    if (flags & NBDKIT_FLAG_FUA) {
      need_flush = true;
      flags &= ~NBDKIT_FLAG_FUA;
    }
    break;
  case NATIVE:
  case PASS:
    break;
  case FORCE:
    flags |= NBDKIT_FLAG_FUA;
    break;
  case DISCARD:
    flags &= ~NBDKIT_FLAG_FUA;
    break;
  }
  r = next->trim (next, count, offs, flags, err);
  if (r != -1 && need_flush)
    r = next->flush (next, 0, err);
  return r;
}

static int
fua_zero (nbdkit_next *next,
          void *handle, uint32_t count, uint64_t offs, uint32_t flags,
          int *err)
{
  int r;
  bool need_flush = false;

  switch (fuamode) {
  case NONE:
    assert (!(flags & NBDKIT_FLAG_FUA));
    break;
  case EMULATE:
    if (flags & NBDKIT_FLAG_FUA) {
      need_flush = true;
      flags &= ~NBDKIT_FLAG_FUA;
    }
    break;
  case NATIVE:
  case PASS:
    break;
  case FORCE:
    flags |= NBDKIT_FLAG_FUA;
    break;
  case DISCARD:
    flags &= ~NBDKIT_FLAG_FUA;
    break;
  }
  r = next->zero (next, count, offs, flags, err);
  if (r != -1 && need_flush)
    r = next->flush (next, 0, err);
  return r;
}

static struct nbdkit_filter filter = {
  .name              = "fua",
  .longname          = "nbdkit fua filter",
  .config            = fua_config,
  .config_help       = fua_config_help,
  .prepare           = fua_prepare,
  .can_flush         = fua_can_flush,
  .can_fua           = fua_can_fua,
  .pwrite            = fua_pwrite,
  .flush             = fua_flush,
  .trim              = fua_trim,
  .zero              = fua_zero,
};

NBDKIT_REGISTER_FILTER (filter)
