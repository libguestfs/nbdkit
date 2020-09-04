/* nbdkit
 * Copyright (C) 2020 Red Hat Inc.
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

#include <string.h>
#include <assert.h>

#include <nbdkit-filter.h>

/* Needed to shut up newer gcc about use of strncpy on our message buffer */
#if __GNUC__ >= 8
#define NONSTRING __attribute__ ((nonstring))
#else
#define NONSTRING
#endif

static char message[512] NONSTRING = "This NBD server requires TLS "
  "authentication before it will serve useful data.\n";

/* Called for each key=value passed on the command line. */
static int
tls_fallback_config (nbdkit_next_config *next, void *nxdata,
                     const char *key, const char *value)
{
  if (strcmp (key, "tlsreadme") == 0) {
    strncpy (message, value, sizeof message); /* Yes, we really mean strncpy */
    return 0;
  }
  return next (nxdata, key, value);
}

#define tls_fallback_config_help \
  "tlsreadme=<MESSAGE>  Alternative contents for the plaintext dummy export.\n"

int
tls_fallback_get_ready (nbdkit_next_get_ready *next, void *nxdata,
                        int thread_model)
{
  if (thread_model == NBDKIT_THREAD_MODEL_SERIALIZE_CONNECTIONS) {
    nbdkit_error ("the tls-fallback filter requires parallel connection "
                  "support");
    return -1;
  }
  return next (nxdata);
}

/* TODO: list_exports needs is_tls parameter */

static const char *
tls_fallback_default_export (nbdkit_next_default_export *next, void *nxdata,
                             int readonly, int is_tls)
{
  if (!is_tls)
    return "";
  return next (nxdata, readonly);
}

/* Helper for determining if this connection is insecure.  This works
 * because we can treat all handles on a binary basis: secure or
 * insecure, which lets .open get away without allocating a more
 * complex handle.
 */
#define NOT_TLS (handle == &message)

static void *
tls_fallback_open (nbdkit_next_open *next, void *nxdata, int readonly,
                   const char *exportname, int is_tls)
{
  /* We do NOT want to call next() when insecure, because we don't
   * know how long it will take.
   */
  if (!is_tls)
    return &message; /* See NOT_TLS for this choice of handle */
  if (next (nxdata, readonly, exportname) == -1)
    return NULL;
  return NBDKIT_HANDLE_NOT_NEEDED;
}

/* When insecure, override any plugin .can_FOO not gated by another in
 * order to avoid an information leak. (can_write gates can_trim,
 * can_zero, can_fast_zero, and can_fua).
 */

static int64_t
tls_fallback_get_size (struct nbdkit_next_ops *next_ops, void *nxdata,
                       void *handle)
{
  if (NOT_TLS)
    return sizeof message;
  return next_ops->get_size (nxdata);
}

static int
tls_fallback_can_write (struct nbdkit_next_ops *next_ops, void *nxdata,
                        void *handle)
{
  if (NOT_TLS)
    return 0;
  return next_ops->can_write (nxdata);
}

static int
tls_fallback_can_flush (struct nbdkit_next_ops *next_ops, void *nxdata,
                        void *handle)
{
  if (NOT_TLS)
    return 0;
  return next_ops->can_flush (nxdata);
}

static int
tls_fallback_is_rotational (struct nbdkit_next_ops *next_ops, void *nxdata,
                            void *handle)
{
  if (NOT_TLS)
    return 0;
  return next_ops->is_rotational (nxdata);
}

static int
tls_fallback_can_extents (struct nbdkit_next_ops *next_ops, void *nxdata,
                          void *handle)
{
  if (NOT_TLS)
    return 0;
  return next_ops->can_extents (nxdata);
}

static int
tls_fallback_can_multi_conn (struct nbdkit_next_ops *next_ops, void *nxdata,
                             void *handle)
{
  if (NOT_TLS)
    return 0;
  return next_ops->can_multi_conn (nxdata);
}

static int
tls_fallback_can_cache (struct nbdkit_next_ops *next_ops, void *nxdata,
                        void *handle)
{
  if (NOT_TLS)
    return NBDKIT_CACHE_NONE;
  return next_ops->can_cache (nxdata);
}

static int
tls_fallback_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
                    void *handle, void *b, uint32_t count, uint64_t offs,
                    uint32_t flags, int *err)
{
  if (NOT_TLS) {
    memcpy (b, message + offs, count);
    return 0;
  }
  return next_ops->pread (nxdata, b, count, offs, flags, err);
}

static struct nbdkit_filter filter = {
  .name              = "tls-fallback",
  .longname          = "nbdkit tls-fallback filter",
  .config            = tls_fallback_config,
  .config_help       = tls_fallback_config_help,
  .get_ready         = tls_fallback_get_ready,
  /* XXX .list_exports needs is_tls parameter */
  .default_export    = tls_fallback_default_export,
  .open              = tls_fallback_open,
  .get_size          = tls_fallback_get_size,
  .can_write         = tls_fallback_can_write,
  .can_flush         = tls_fallback_can_flush,
  .is_rotational     = tls_fallback_is_rotational,
  .can_extents       = tls_fallback_can_extents,
  .can_multi_conn    = tls_fallback_can_multi_conn,
  .can_cache         = tls_fallback_can_cache,
  .pread             = tls_fallback_pread,
};

NBDKIT_REGISTER_FILTER(filter)
