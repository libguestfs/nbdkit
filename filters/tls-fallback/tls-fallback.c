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
tls_fallback_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
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
tls_fallback_get_ready (int thread_model)
{
  if (thread_model == NBDKIT_THREAD_MODEL_SERIALIZE_CONNECTIONS) {
    nbdkit_error ("the tls-fallback filter requires parallel connection "
                  "support");
    return -1;
  }
  return 0;
}

static int
tls_fallback_list_exports (nbdkit_next_list_exports *next,
                           nbdkit_backend *nxdata,
                           int readonly, int is_tls,
                           struct nbdkit_exports *exports)
{
  if (!is_tls)
    return nbdkit_add_export (exports, "", NULL);
  return next (nxdata, readonly, exports);
}

static const char *
tls_fallback_default_export (nbdkit_next_default_export *next,
                             nbdkit_backend *nxdata,
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
tls_fallback_open (nbdkit_next_open *next, nbdkit_context *nxdata,
                   int readonly,
                   const char *exportname, int is_tls)
{
  /* We do NOT want to call next() when insecure, because we don't
   * know how long it will take.  See also CVE-2019-14850 in
   * nbdkit-security.pod.  But that means that this filter must
   * override every possible callback that can be reached during
   * handshake, to avoid passing through a non-TLS call to a missing
   * backend.
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

static const char *
tls_fallback_export_description (nbdkit_next *next, void *handle)
{
  if (NOT_TLS)
    return NULL;
  return next->export_description (next);
}

static int64_t
tls_fallback_get_size (nbdkit_next *next,
                       void *handle)
{
  if (NOT_TLS)
    return sizeof message;
  return next->get_size (next);
}

static int
tls_fallback_block_size (nbdkit_next *next,
                         void *handle,
                         uint32_t *minimum,
                         uint32_t *preferred,
                         uint32_t *maximum)
{
  if (NOT_TLS) {
    *minimum = *preferred = *maximum = 0;
    return 0;
  }
  return next->block_size (next, minimum, preferred, maximum);
}

static int
tls_fallback_can_write (nbdkit_next *next,
                        void *handle)
{
  if (NOT_TLS)
    return 0;
  return next->can_write (next);
}

static int
tls_fallback_can_flush (nbdkit_next *next,
                        void *handle)
{
  if (NOT_TLS)
    return 0;
  return next->can_flush (next);
}

static int
tls_fallback_is_rotational (nbdkit_next *next,
                            void *handle)
{
  if (NOT_TLS)
    return 0;
  return next->is_rotational (next);
}

static int
tls_fallback_can_extents (nbdkit_next *next,
                          void *handle)
{
  if (NOT_TLS)
    return 0;
  return next->can_extents (next);
}

static int
tls_fallback_can_multi_conn (nbdkit_next *next,
                             void *handle)
{
  if (NOT_TLS)
    return 0;
  return next->can_multi_conn (next);
}

static int
tls_fallback_can_cache (nbdkit_next *next,
                        void *handle)
{
  if (NOT_TLS)
    return NBDKIT_CACHE_NONE;
  return next->can_cache (next);
}

static int
tls_fallback_pread (nbdkit_next *next,
                    void *handle, void *b, uint32_t count, uint64_t offs,
                    uint32_t flags, int *err)
{
  if (NOT_TLS) {
    memcpy (b, message + offs, count);
    return 0;
  }
  return next->pread (next, b, count, offs, flags, err);
}

static struct nbdkit_filter filter = {
  .name               = "tls-fallback",
  .longname           = "nbdkit tls-fallback filter",
  .config             = tls_fallback_config,
  .config_help        = tls_fallback_config_help,
  .get_ready          = tls_fallback_get_ready,
  .list_exports       = tls_fallback_list_exports,
  .default_export     = tls_fallback_default_export,
  .open               = tls_fallback_open,
  .export_description = tls_fallback_export_description,
  .get_size           = tls_fallback_get_size,
  .block_size         = tls_fallback_block_size,
  .can_write          = tls_fallback_can_write,
  .can_flush          = tls_fallback_can_flush,
  .is_rotational      = tls_fallback_is_rotational,
  .can_extents        = tls_fallback_can_extents,
  .can_multi_conn     = tls_fallback_can_multi_conn,
  .can_cache          = tls_fallback_can_cache,
  .pread              = tls_fallback_pread,
};

NBDKIT_REGISTER_FILTER (filter)
