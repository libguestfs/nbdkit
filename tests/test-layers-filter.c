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
#undef NDEBUG /* Keep test strong even for nbdkit built without assertions */
#include <assert.h>

#include <nbdkit-filter.h>

#define str(s) #s
#define DEBUG_FUNCTION nbdkit_debug ("%s: %s", layer, __func__)

/* Perform sanity checking on nbdkit_next stability */
static nbdkit_backend *saved_backend;
struct handle {
  nbdkit_next *next;
};

static void
test_layers_filter_load (void)
{
  DEBUG_FUNCTION;
}

static void
test_layers_filter_unload (void)
{
  DEBUG_FUNCTION;
}

static int
test_layers_filter_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
                           const char *key, const char *value)
{
  DEBUG_FUNCTION;
  return next (nxdata, key, value);
}

static int
test_layers_filter_config_complete (nbdkit_next_config_complete *next,
                                    nbdkit_backend *nxdata)
{
  DEBUG_FUNCTION;
  return next (nxdata);
}

#define test_layers_filter_config_help          \
  "test_layers_" layer "_config_help"

static int
test_layers_filter_thread_model (void)
{
  DEBUG_FUNCTION;
  return NBDKIT_THREAD_MODEL_PARALLEL;
}

static int
test_layers_filter_get_ready (int thread_model)
{
  DEBUG_FUNCTION;
  return 0;
}

static int
test_layers_filter_after_fork (nbdkit_backend *backend)
{
  DEBUG_FUNCTION;
  saved_backend = backend;
  return 0;
}

static void
test_layers_filter_cleanup (nbdkit_backend *backend)
{
  assert (backend == saved_backend);
  DEBUG_FUNCTION;
}

static int
test_layers_filter_preconnect (nbdkit_next_preconnect *next,
                               nbdkit_backend *nxdata, int readonly)
{
  assert (nxdata == saved_backend);
  DEBUG_FUNCTION;
  return next (nxdata, readonly);
}

static int
test_layers_filter_list_exports (nbdkit_next_list_exports *next,
                                 nbdkit_backend *nxdata,
                                 int readonly, int is_tls,
                                 struct nbdkit_exports *exports)
{
  assert (nxdata == saved_backend);
  DEBUG_FUNCTION;
  return next (nxdata, readonly, exports);
}

static const char *
test_layers_filter_default_export (nbdkit_next_default_export *next,
                                   nbdkit_backend *nxdata, int readonly,
                                   int is_tls)
{
  assert (nxdata == saved_backend);
  DEBUG_FUNCTION;
  return next (nxdata, readonly);
}

static void *
test_layers_filter_open (nbdkit_next_open *next, nbdkit_context *nxdata,
                         int readonly, const char *exportname, int is_tls)
{
  struct handle *h = calloc (1, sizeof *h);

  assert (nbdkit_context_get_backend (nxdata) == saved_backend);
  if (!h) {
    perror ("malloc");
    exit (1);
  }

  /* Demonstrate our claim that next() is sugar for open-coding. */
  if (strcmp (layer, "filter2") == 0) {
    nbdkit_backend *backend;
    nbdkit_next *n, *old;

    backend = nbdkit_context_get_backend (nxdata);
    assert (backend != NULL);
    n = nbdkit_next_context_open (backend, readonly, exportname, 0);
    if (n == NULL) {
      free (h);
      return NULL;
    }
    old = nbdkit_context_set_next (nxdata, n);
    assert (old == NULL);
    h->next = n;
  }
  else if (next (nxdata, readonly, exportname) == -1) {
    free (h);
    return NULL;
  }

  /* Debug after recursing, to show opposite order from .close */
  DEBUG_FUNCTION;

  return h;
}

static void
test_layers_filter_close (void *handle)
{
  DEBUG_FUNCTION;
  free (handle);
}

static int
test_layers_filter_prepare (nbdkit_next *next,
                            void *handle, int readonly)
{
  struct handle *h = handle;

  if (strcmp (layer, "filter2") == 0)
    assert (h->next == next);
  else {
    assert (h->next == NULL);
    h->next = next;
  }
  DEBUG_FUNCTION;
  return 0;
}

static int
test_layers_filter_finalize (nbdkit_next *next,
                             void *handle)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return 0;
}

static int64_t
test_layers_filter_get_size (nbdkit_next *next,
                             void *handle)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->get_size (next);
}

static int
test_layers_filter_can_write (nbdkit_next *next,
                              void *handle)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->can_write (next);
}

static int
test_layers_filter_can_flush (nbdkit_next *next,
                              void *handle)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->can_flush (next);
}

static int
test_layers_filter_is_rotational (nbdkit_next *next,
                                  void *handle)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->is_rotational (next);
}

static int
test_layers_filter_can_trim (nbdkit_next *next,
                             void *handle)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->can_trim (next);
}

static int
test_layers_filter_can_zero (nbdkit_next *next,
                             void *handle)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->can_zero (next);
}

static int
test_layers_filter_can_fast_zero (nbdkit_next *next, void *handle)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->can_fast_zero (next);
}

static int
test_layers_filter_can_fua (nbdkit_next *next,
                            void *handle)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->can_fua (next);
}

static int
test_layers_filter_can_multi_conn (nbdkit_next *next,
                                   void *handle)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->can_multi_conn (next);
}

static int
test_layers_filter_can_extents (nbdkit_next *next,
                                void *handle)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->can_extents (next);
}

static int
test_layers_filter_can_cache (nbdkit_next *next,
                              void *handle)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->can_cache (next);
}

static int
test_layers_filter_pread (nbdkit_next *next,
                          void *handle, void *buf,
                          uint32_t count, uint64_t offset,
                          uint32_t flags, int *err)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->pread (next, buf, count, offset, flags, err);
}

static int
test_layers_filter_pwrite (nbdkit_next *next,
                           void *handle,
                           const void *buf, uint32_t count, uint64_t offset,
                           uint32_t flags, int *err)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->pwrite (next, buf, count, offset, flags, err);
}

static int
test_layers_filter_flush (nbdkit_next *next,
                          void *handle,
                          uint32_t flags, int *err)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->flush (next, flags, err);
}

static int
test_layers_filter_trim (nbdkit_next *next,
                         void *handle, uint32_t count, uint64_t offset,
                         uint32_t flags, int *err)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->trim (next, count, offset, flags, err);
}

static int
test_layers_filter_zero (nbdkit_next *next,
                         void *handle, uint32_t count, uint64_t offset,
                         uint32_t flags, int *err)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->zero (next, count, offset, flags, err);
}

static int
test_layers_filter_extents (nbdkit_next *next,
                            void *handle, uint32_t count, uint64_t offset,
                            uint32_t flags, struct nbdkit_extents *extents,
                            int *err)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->extents (next, count, offset, flags, extents, err);
}

static int
test_layers_filter_cache (nbdkit_next *next,
                          void *handle, uint32_t count, uint64_t offset,
                          uint32_t flags, int *err)
{
  struct handle *h = handle;

  assert (h->next == next);
  DEBUG_FUNCTION;
  return next->cache (next, count, offset, flags, err);
}

static struct nbdkit_filter filter = {
  .name              = "testlayers" layer,
  .load              = test_layers_filter_load,
  .unload            = test_layers_filter_unload,
  .config            = test_layers_filter_config,
  .config_complete   = test_layers_filter_config_complete,
  .config_help       = test_layers_filter_config_help,
  .thread_model      = test_layers_filter_thread_model,
  .get_ready         = test_layers_filter_get_ready,
  .after_fork        = test_layers_filter_after_fork,
  .cleanup           = test_layers_filter_cleanup,
  .preconnect        = test_layers_filter_preconnect,
  .list_exports      = test_layers_filter_list_exports,
  .default_export    = test_layers_filter_default_export,
  .open              = test_layers_filter_open,
  .close             = test_layers_filter_close,
  .prepare           = test_layers_filter_prepare,
  .finalize          = test_layers_filter_finalize,
  .get_size          = test_layers_filter_get_size,
  .can_write         = test_layers_filter_can_write,
  .can_flush         = test_layers_filter_can_flush,
  .is_rotational     = test_layers_filter_is_rotational,
  .can_trim          = test_layers_filter_can_trim,
  .can_zero          = test_layers_filter_can_zero,
  .can_fast_zero     = test_layers_filter_can_fast_zero,
  .can_fua           = test_layers_filter_can_fua,
  .can_multi_conn    = test_layers_filter_can_multi_conn,
  .can_extents       = test_layers_filter_can_extents,
  .can_cache         = test_layers_filter_can_cache,
  .pread             = test_layers_filter_pread,
  .pwrite            = test_layers_filter_pwrite,
  .flush             = test_layers_filter_flush,
  .trim              = test_layers_filter_trim,
  .zero              = test_layers_filter_zero,
  .extents           = test_layers_filter_extents,
  .cache             = test_layers_filter_cache,
};

NBDKIT_REGISTER_FILTER (filter)
