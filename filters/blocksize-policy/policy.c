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
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#include <nbdkit-filter.h>

#include "ispowerof2.h"
#include "rounding.h"
#include "windows-compat.h"

/* Block size constraints configured on the command line (0 = unset). */
static uint32_t config_minimum;
static uint32_t config_preferred;
static uint32_t config_maximum;
static uint32_t config_disconnect;

/* Error policy. */
static enum { EP_ALLOW, EP_ERROR } error_policy = EP_ALLOW;

static int
policy_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
               const char *key, const char *value)
{
  int64_t r;

  if (strcmp (key, "blocksize-error-policy") == 0) {
    if (strcmp (value, "allow") == 0)
      error_policy = EP_ALLOW;
    else if (strcmp (value, "error") == 0)
      error_policy = EP_ERROR;
    else {
      nbdkit_error ("unknown %s: %s", key, value);
      return -1;
    }
    return 0;
  }
  else if (strcmp (key, "blocksize-minimum") == 0) {
    r = nbdkit_parse_size (value);
    if (r == -1 || r > UINT32_MAX) {
    parse_error:
      nbdkit_error ("%s: could not parse %s", key, value);
      return -1;
    }
    config_minimum = r;
    return 0;
  }
  else if (strcmp (key, "blocksize-preferred") == 0) {
    r = nbdkit_parse_size (value);
    if (r == -1 || r > UINT32_MAX) goto parse_error;
    config_preferred = r;
    return 0;
  }
  else if (strcmp (key, "blocksize-maximum") == 0) {
    r = nbdkit_parse_size (value);
    if (r == -1 || r > UINT32_MAX) goto parse_error;
    config_maximum = r;
    return 0;
  }
  else if (strcmp (key, "blocksize-write-disconnect") == 0) {
    r = nbdkit_parse_size (value);
    if (r == -1 || r > UINT32_MAX) goto parse_error;
    config_disconnect = r;
    return 0;
  }

  return next (nxdata, key, value);
}

static int
policy_config_complete (nbdkit_next_config_complete *next,
                        nbdkit_backend *nxdata)
{
  /* These checks roughly reflect the same checks made in
   * server/plugins.c: plugin_block_size
   */

  if (config_minimum) {
    if (! is_power_of_2 (config_minimum)) {
      nbdkit_error ("blocksize-minimum must be a power of 2");
      return -1;
    }
    if (config_minimum > 65536) {
      nbdkit_error ("blocksize-minimum must be <= 64K");
      return -1;
    }
  }

  if (config_preferred) {
    if (! is_power_of_2 (config_preferred)) {
      nbdkit_error ("blocksize-preferred must be a power of 2");
      return -1;
    }
    if (config_preferred < 512 || config_preferred > 32 * 1024 * 1024) {
      nbdkit_error ("blocksize-preferred must be between 512 and 32M");
      return -1;
    }
  }

  if (config_minimum && config_maximum) {
    if (config_maximum != (uint32_t)-1 &&
        (config_maximum % config_maximum) != 0) {
      nbdkit_error ("blocksize-maximum must be -1 "
                    "or a multiple of blocksize-minimum");
      return -1;
    }
  }

  if (config_minimum && config_preferred) {
    if (config_minimum > config_preferred) {
      nbdkit_error ("blocksize-minimum must be <= blocksize-preferred");
      return -1;
    }
  }

  if (config_preferred && config_maximum) {
    if (config_preferred > config_maximum) {
      nbdkit_error ("blocksize-preferred must be <= blocksize-maximum");
      return -1;
    }
  }

  if (config_minimum && config_disconnect) {
    if (config_disconnect <= config_minimum) {
      nbdkit_error ("blocksize-write-disonnect must be larger than "
                    "blocksize-minimum");
      return -1;
    }
  }

  return next (nxdata);
}

static int
policy_block_size (nbdkit_next *next, void *handle,
                   uint32_t *minimum, uint32_t *preferred, uint32_t *maximum)
{
  /* If the user has set all of the block size parameters then we
   * don't need to ask the plugin, we can go ahead and advertise them.
   */
  if (config_minimum && config_preferred && config_maximum) {
    *minimum = config_minimum;
    *preferred = config_preferred;
    *maximum = config_maximum;
    return 0;
  }

  /* Otherwise, ask the plugin. */
  if (next->block_size (next, minimum, preferred, maximum) == -1)
    return -1;

  /* If the user of this filter didn't configure anything, then return
   * the plugin values (even if unset).
   */
  if (!config_minimum && !config_preferred && !config_maximum)
    return 0;

  /* Now we get to the awkward case where the user configured some
   * values but not others.  There's all kinds of room for things to
   * go wrong here, so try to check for obvious user errors as best we
   * can.
   */
  if (*minimum == 0) {           /* Plugin didn't set anything. */
    if (config_minimum)
      *minimum = config_minimum;
    else
      *minimum = 1;

    if (config_preferred)
      *preferred = config_preferred;
    else
      *preferred = 4096;

    if (config_maximum)
      *maximum = config_maximum;
    else if (config_disconnect)
      *maximum = ROUND_DOWN (config_disconnect, *minimum);
    else
      *maximum = 0xffffffff;
  }
  else {                        /* Plugin set some values. */
    if (config_minimum)
      *minimum = config_minimum;

    if (config_preferred)
      *preferred = config_preferred;

    if (config_maximum)
      *maximum = config_maximum;
  }

  if (*minimum > *preferred || *preferred > *maximum) {
    nbdkit_error ("computed block size values are invalid, minimum %" PRIu32
                  " > preferred %" PRIu32
                  " or preferred > maximum %" PRIu32,
                  *minimum, *preferred, *maximum);
    return -1;
  }
  return 0;
}

/* This function checks the error policy for all request functions
 * below.
 *
 * The 'data' flag is true for pread and pwrite (where we check the
 * maximum bound).  We don't check maximum for non-data-carrying
 * calls like zero.
 *
 * The NBD specification mandates EINVAL for block size constraint
 * problems.
 */
static int
check_policy (nbdkit_next *next, void *handle,
              const char *type, bool data,
              uint32_t count, uint64_t offset, int *err)
{
  uint32_t minimum, preferred, maximum;

  if (error_policy == EP_ALLOW)
    return 0;

  /* Get the current block size constraints.  Note these are cached in
   * the backend so if they've already been computed then this simply
   * returns the cached values.  The plugin is only asked once per
   * connection.
   */
  errno = 0;
  if (policy_block_size (next, handle,
                         &minimum, &preferred, &maximum) == -1) {
    *err = errno ? : EINVAL;
    return -1;
  }

  /* If there are no constraints, allow. */
  if (minimum == 0)
    return 0;

  /* Check constraints. */
  if (count < minimum) {
    *err = EINVAL;
    nbdkit_error ("client %s request rejected: "
                  "count %" PRIu32 " is smaller than minimum size %" PRIu32,
                  type, count, minimum);
    return -1;
  }
  if (data && count > maximum) {  /* Only do this for pread/pwrite. */
    *err = EINVAL;
    nbdkit_error ("client %s request rejected: "
                  "count %" PRIu32 " is larger than maximum size %" PRIu32,
                  type, count, maximum);
    return -1;
  }
  if ((count % minimum) != 0) {
    *err = EINVAL;
    nbdkit_error ("client %s request rejected: "
                  "count %" PRIu32 " is not a multiple "
                  "of minimum size %" PRIu32,
                  type, count, minimum);
    return -1;
  }
  if ((offset % minimum) != 0) {
    *err = EINVAL;
    nbdkit_error ("client %s request rejected: "
                  "offset %" PRIu64 " is not aligned to a multiple "
                  "of minimum size %" PRIu32,
                  type, offset, minimum);
    return -1;
  }

  return 0;
}

static int
policy_pread (nbdkit_next *next,
              void *handle, void *buf, uint32_t count, uint64_t offset,
              uint32_t flags, int *err)
{
  if (check_policy (next, handle, "pread", true, count, offset, err) == -1)
    return -1;

  return next->pread (next, buf, count, offset, flags, err);
}

static int
policy_pwrite (nbdkit_next *next,
               void *handle, const void *buf, uint32_t count, uint64_t offset,
               uint32_t flags, int *err)
{
  if (config_disconnect && count > config_disconnect) {
    nbdkit_error ("disconnecting client due to oversize write request");
    nbdkit_disconnect (true);
    *err = ESHUTDOWN;
    return -1;
  }

  if (check_policy (next, handle, "pwrite", true, count, offset, err) == -1)
    return -1;

  return next->pwrite (next, buf, count, offset, flags, err);
}

static int
policy_zero (nbdkit_next *next,
             void *handle, uint32_t count, uint64_t offset, uint32_t flags,
             int *err)
{
  if (check_policy (next, handle, "zero", false, count, offset, err) == -1)
    return -1;

  return next->zero (next, count, offset, flags, err);
}

static int
policy_trim (nbdkit_next *next,
             void *handle, uint32_t count, uint64_t offset, uint32_t flags,
             int *err)
{
  if (check_policy (next, handle, "trim", false, count, offset, err) == -1)
    return -1;

  return next->trim (next, count, offset, flags, err);
}

static int
policy_cache (nbdkit_next *next,
              void *handle, uint32_t count, uint64_t offset,
              uint32_t flags, int *err)
{
  if (check_policy (next, handle, "cache", false, count, offset, err) == -1)
    return -1;

  return next->cache (next, count, offset, flags, err);
}

static int
policy_extents (nbdkit_next *next,
                void *handle, uint32_t count, uint64_t offset, uint32_t flags,
                struct nbdkit_extents *extents, int *err)
{
  if (check_policy (next, handle, "extents", false, count, offset, err) == -1)
    return -1;

  return next->extents (next, count, offset, flags, extents, err);
}

static struct nbdkit_filter filter = {
  .name              = "blocksize-policy",
  .longname          = "nbdkit blocksize policy filter",
  .config            = policy_config,
  .config_complete   = policy_config_complete,

  .block_size        = policy_block_size,

  .pread             = policy_pread,
  .pwrite            = policy_pwrite,
  .zero              = policy_zero,
  .trim              = policy_trim,
  .cache             = policy_cache,
  .extents           = policy_extents,
};

NBDKIT_REGISTER_FILTER (filter)
