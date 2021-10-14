/* nbdkit
 * Copyright (C) 2019-2021 Red Hat Inc.
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
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <sys/time.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "windows-compat.h"

static unsigned retries = 2;    /* 0 = filter is disabled */
static unsigned delay = 2;
static bool retry_open_call = true;

static int
retry_request_thread_model (void)
{
  return NBDKIT_THREAD_MODEL_PARALLEL;
}

static int
retry_request_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
                      const char *key, const char *value)
{
  int r;

  if (strcmp (key, "retry-request-retries") == 0) {
    if (nbdkit_parse_unsigned ("retry-request-retries", value, &retries) == -1)
      return -1;
    if (retries > 1000) {
      nbdkit_error ("retry-request-retries: value too large");
      return -1;
    }
    return 0;
  }
  else if (strcmp (key, "retry-request-delay") == 0) {
    if (nbdkit_parse_unsigned ("retry-request-delay", value, &delay) == -1)
      return -1;
    if (delay == 0) {
      nbdkit_error ("retry-request-delay cannot be 0");
      return -1;
    }
    return 0;
  }
  else if (strcmp (key, "retry-request-open") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    retry_open_call = r;
    return 0;
  }

  return next (nxdata, key, value);
}

#define retry_request_config_help \
  "retry-request-retries=<N> Number of retries (default: 2).\n" \
  "retry-request-delay=<N>   Seconds to wait before retry (default: 2).\n" \
  "retry-request-open=false  Do not retry opening the plugin (default: true).\n"

/* These macros encapsulate the logic of retrying.
 *
 * The code between RETRY_START...RETRY_END must set r to 0 or -1 on
 * success or failure.  *err may also be implicitly assigned.
 */
#define RETRY_START                                                     \
  {                                                                     \
    unsigned i;                                                         \
                                                                        \
    r = -1;                                                             \
    for (i = 0; r == -1 && i <= retries; ++i) {                         \
      if (i > 0) {                                                      \
        nbdkit_debug ("retry %u: waiting %u seconds before retrying",   \
                      i, delay);                                        \
        if (nbdkit_nanosleep (delay, 0) == -1) {                        \
          if (*err == 0)                                                \
            *err = errno;                                               \
          break;                                                        \
        }                                                               \
      }                                                                 \
      do
#define RETRY_END                                                       \
      while (0);                                                        \
    }                                                                   \
  }

static void *
retry_request_open (nbdkit_next_open *next, nbdkit_context *nxdata,
                    int readonly, const char *exportname, int is_tls)
{
  int r;

  if (retry_open_call) {
    int *err = &errno;          /* used by the RETRY_* macros */

    RETRY_START
      r = next (nxdata, readonly, exportname);
    RETRY_END;
  }
  else {
    r = next (nxdata, readonly, exportname);
  }

  return r == 0 ? NBDKIT_HANDLE_NOT_NEEDED : NULL;
}

static int
retry_request_pread (nbdkit_next *next,
                     void *handle, void *buf, uint32_t count, uint64_t offset,
                     uint32_t flags, int *err)
{
  int r;

  RETRY_START
    r = next->pread (next, buf, count, offset, flags, err);
  RETRY_END;
  return r;
}

static int
retry_request_pwrite (nbdkit_next *next,
                      void *handle,
                      const void *buf, uint32_t count, uint64_t offset,
                      uint32_t flags, int *err)
{
  int r;

  RETRY_START
    r = next->pwrite (next, buf, count, offset, flags, err);
  RETRY_END;
  return r;
}

static int
retry_request_trim (nbdkit_next *next,
                    void *handle,
                    uint32_t count, uint64_t offset, uint32_t flags,
                    int *err)
{
  int r;

  RETRY_START
    r = next->trim (next, count, offset, flags, err);
  RETRY_END;
  return r;
}

static int
retry_request_flush (nbdkit_next *next,
                     void *handle, uint32_t flags,
                     int *err)
{
  int r;

  RETRY_START
    r = next->flush (next, flags, err);
  RETRY_END;
  return r;
}

static int
retry_request_zero (nbdkit_next *next,
                    void *handle,
                    uint32_t count, uint64_t offset, uint32_t flags,
                    int *err)
{
  int r;

  RETRY_START
    r = next->zero (next, count, offset, flags, err);
  RETRY_END;
  return r;
}

static int
retry_request_extents (nbdkit_next *next,
                       void *handle,
                       uint32_t count, uint64_t offset, uint32_t flags,
                       struct nbdkit_extents *extents, int *err)
{
  CLEANUP_EXTENTS_FREE struct nbdkit_extents *extents2 = NULL;
  int r;

  RETRY_START {
    /* Each retry must begin with extents reset to the right beginning. */
    nbdkit_extents_free (extents2);
    extents2 = nbdkit_extents_new (offset, next->get_size (next));
    if (extents2 == NULL) {
      *err = errno;
      return -1; /* Not worth a retry after ENOMEM. */
    }
    r = next->extents (next, count, offset, flags, extents2, err);
  } RETRY_END;

  if (r == 0) {
    size_t i;

    /* Transfer the successful extents back to the caller. */
    for (i = 0; i < nbdkit_extents_count (extents2); ++i) {
      struct nbdkit_extent e = nbdkit_get_extent (extents2, i);

      if (nbdkit_add_extent (extents, e.offset, e.length, e.type) == -1) {
        *err = errno;
        return -1;
      }
    }
  }

  return r;
}

static int
retry_request_cache (nbdkit_next *next,
                     void *handle,
                     uint32_t count, uint64_t offset, uint32_t flags,
                     int *err)
{
  int r;

  RETRY_START
    r = next->cache (next, count, offset, flags, err);
  RETRY_END;
  return r;
}

static struct nbdkit_filter filter = {
  .name              = "retry-request",
  .longname          = "nbdkit retry request filter",
  .thread_model      = retry_request_thread_model,
  .config            = retry_request_config,
  .config_help       = retry_request_config_help,
  .open              = retry_request_open,
  .pread             = retry_request_pread,
  .pwrite            = retry_request_pwrite,
  .trim              = retry_request_trim,
  .flush             = retry_request_flush,
  .zero              = retry_request_zero,
  .extents           = retry_request_extents,
  .cache             = retry_request_cache,
};

NBDKIT_REGISTER_FILTER(filter)
