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

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "vector.h"

#include "internal.h"

/* Cap nr_exports to avoid sending over-large replies to the client,
 * and to avoid a plugin with large list consuming too much memory.
 */
#define MAX_EXPORTS 10000

/* Appendable list of exports. */
DEFINE_VECTOR_TYPE (exports, struct nbdkit_export);

struct nbdkit_exports {
  exports exports;
  bool use_default;
};

NBDKIT_DLL_PUBLIC struct nbdkit_exports *
nbdkit_exports_new (void)
{
  struct nbdkit_exports *r;

  r = malloc (sizeof *r);
  if (r == NULL) {
    nbdkit_error ("nbdkit_exports_new: malloc: %m");
    return NULL;
  }
  r->exports = (exports) empty_vector;
  r->use_default = false;
  return r;
}

static void
nbdkit_export_clear (struct nbdkit_export exp)
{
  free (exp.name);
  free (exp.description);
}

NBDKIT_DLL_PUBLIC void
nbdkit_exports_free (struct nbdkit_exports *exps)
{
  if (exps) {
    exports_iter (&exps->exports, nbdkit_export_clear);
    free (exps->exports.ptr);
    free (exps);
  }
}

NBDKIT_DLL_PUBLIC size_t
nbdkit_exports_count (const struct nbdkit_exports *exps)
{
  return exps->exports.len;
}

NBDKIT_DLL_PUBLIC const struct nbdkit_export
nbdkit_get_export (const struct nbdkit_exports *exps, size_t i)
{
  assert (i < exps->exports.len);
  return exps->exports.ptr[i];
}

NBDKIT_DLL_PUBLIC int
nbdkit_add_export (struct nbdkit_exports *exps,
                   const char *name, const char *description)
{
  struct nbdkit_export e = { NULL, NULL };

  if (exps->exports.len == MAX_EXPORTS) {
    nbdkit_error ("nbdkit_add_export: too many exports");
    errno = EINVAL;
    return -1;
  }
  if (strlen (name) > NBD_MAX_STRING ||
      (description && strlen (description) > NBD_MAX_STRING)) {
    nbdkit_error ("nbdkit_add_export: string too long");
    errno = EINVAL;
    return -1;
  }

  e.name = strdup (name);
  if (e.name == NULL) {
    nbdkit_error ("nbdkit_add_export: strdup: %m");
    return -1;
  }
  if (description) {
    e.description = strdup (description);
    if (e.description == NULL) {
      nbdkit_error ("nbdkit_add_export: strdup: %m");
      free (e.name);
      errno = ENOMEM;
      return -1;
    }
  }

  if (exports_append (&exps->exports, e) == -1) {
    nbdkit_error ("nbdkit_add_export: realloc: %m");
    free (e.name);
    free (e.description);
    errno = ENOMEM;
    return -1;
  }

  return 0;
}

NBDKIT_DLL_PUBLIC int
nbdkit_use_default_export (struct nbdkit_exports *exps)
{
  exps->use_default = true;
  return 0;
}

int
exports_resolve_default (struct nbdkit_exports *exps, struct backend *b,
                         int readonly)
{
  const char *def = NULL;

  if (exps->use_default) {
    def = backend_default_export (b, readonly);
    exps->use_default = false;
  }
  if (def)
    return nbdkit_add_export (exps, def, NULL);
  return 0;
}
