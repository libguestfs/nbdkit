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

static enum FuaMode {
  NONE,
  EMULATE,
  NATIVE,
  FORCE,
} fuamode;

static int
fua_config (nbdkit_next_config *next, void *nxdata,
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
    else {
      nbdkit_error ("unknown fuamode '%s'", value);
      return -1;
    }
    return 0;
  }
  return next (nxdata, key, value);
}

#define fua_config_help \
  "fuamode=<MODE>       One of 'none' (default), 'emulate', 'native', 'force'.\n" \

/* Check that desired mode is supported by plugin. */
static int
fua_prepare (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle,
             int readonly)
{
  int r;

  /* If we are opened readonly, this filter has no impact */
  if (readonly)
    return 0;

  switch (fuamode) {
  case NONE:
    break;
  case EMULATE:
    r = next_ops->can_flush (nxdata);
    if (r == -1)
      return -1;
    if (r == 0) {
      nbdkit_error ("fuamode 'emulate' requires plugin flush support");
      return -1;
    }
    break;
  case NATIVE:
  case FORCE:
    r = next_ops->can_fua (nxdata);
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
fua_can_flush (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  if (fuamode == FORCE)
    return 1; /* Advertise our no-op flush, even if plugin lacks it */
  return next_ops->can_flush (nxdata);
}

/* Advertise desired fua mode. */
static int
fua_can_fua (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  switch (fuamode) {
  case NONE:
    return NBDKIT_FUA_NONE;
  case EMULATE:
    return NBDKIT_FUA_EMULATE;
  case NATIVE:
  case FORCE:
    return NBDKIT_FUA_NATIVE;
  }
  abort ();
}

static int
fua_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
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
    break;
  case FORCE:
    flags |= NBDKIT_FLAG_FUA;
    break;
  }
  r = next_ops->pwrite (nxdata, buf, count, offs, flags, err);
  if (r != -1 && need_flush)
    r = next_ops->flush (nxdata, 0, err);
  return r;
}

static int
fua_flush (struct nbdkit_next_ops *next_ops, void *nxdata,
           void *handle, uint32_t flags, int *err)
{
  if (fuamode == FORCE)
    return 0; /* Nothing to flush, since all writes already used FUA */
  return next_ops->flush (nxdata, flags, err);
}

static int
fua_trim (struct nbdkit_next_ops *next_ops, void *nxdata,
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
    break;
  case FORCE:
    flags |= NBDKIT_FLAG_FUA;
    break;
  }
  r = next_ops->trim (nxdata, count, offs, flags, err);
  if (r != -1 && need_flush)
    r = next_ops->flush (nxdata, 0, err);
  return r;
}

static int
fua_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
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
    break;
  case FORCE:
    flags |= NBDKIT_FLAG_FUA;
    break;
  }
  r = next_ops->zero (nxdata, count, offs, flags, err);
  if (r != -1 && need_flush)
    r = next_ops->flush (nxdata, 0, err);
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

NBDKIT_REGISTER_FILTER(filter)
