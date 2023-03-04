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

static int thread_model = NBDKIT_THREAD_MODEL_SERIALIZE_REQUESTS;

static int
noparallel_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
                   const char *key, const char *value)
{
  if (strcmp (key, "serialize") == 0 ||
      strcmp (key, "serialise") == 0) {
    if (strcmp (value, "connections") == 0)
      thread_model = NBDKIT_THREAD_MODEL_SERIALIZE_CONNECTIONS;
    else if (strcmp (value, "all_requests") == 0 ||
             strcmp (value, "all-requests") == 0)
      thread_model = NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS;
    else if (strcmp (value, "requests") != 0) {
      nbdkit_error ("unknown noparallel serialize mode '%s'", value);
      return -1;
    }
    return 0;
  }
  return next (nxdata, key, value);
}

#define noparallel_config_help \
  "serialize=<MODE>      'requests' (default), 'all-requests', or 'connections'.\n" \

/* Apply runtime reduction to thread model. */
static int
noparallel_thread_model (void)
{
  return thread_model;
}

static struct nbdkit_filter filter = {
  .name              = "noparallel",
  .longname          = "nbdkit noparallel filter",
  .config            = noparallel_config,
  .config_help       = noparallel_config_help,
  .thread_model      = noparallel_thread_model,
};

NBDKIT_REGISTER_FILTER (filter)
