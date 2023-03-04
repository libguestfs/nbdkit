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
#include <string.h>
#include <pthread.h>

#include <nbdkit-filter.h>

#include "cleanup.h"

/* Counts client connections. */
static unsigned connections;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* Client limit (0 = filter is disabled). */
static unsigned limit = 1;

static int
limit_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
              const char *key, const char *value)
{
  if (strcmp (key, "limit") == 0) {
    if (nbdkit_parse_unsigned ("limit", value, &limit) == -1)
      return -1;
    return 0;
  }

  return next (nxdata, key, value);
}

static void
too_many_clients_error (void)
{
  nbdkit_error ("limit: too many clients connected, connection rejected");
}

/* We limit connections in the preconnect stage (in particular before
 * any heavyweight NBD or TLS negotiations has been done).  However we
 * count connections in the open/close calls since clients can drop
 * out between preconnect and open.
 */
static int
limit_preconnect (nbdkit_next_preconnect *next, nbdkit_backend *nxdata,
                  int readonly)
{
  if (next (nxdata, readonly) == -1)
    return -1;

  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);

  if (limit > 0 && connections >= limit) {
    too_many_clients_error ();
    return -1;
  }

  return 0;
}

static void *
limit_open (nbdkit_next_open *next, nbdkit_context *nxdata,
            int readonly, const char *exportname, int is_tls)
{
  if (next (nxdata, readonly, exportname) == -1)
    return NULL;

  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);

  /* We have to check again because clients can artificially slow down
   * the NBD negotiation in order to bypass the limit otherwise.
   */
  if (limit > 0 && connections >= limit) {
    too_many_clients_error ();
    return NULL;
  }

  connections++;
  return NBDKIT_HANDLE_NOT_NEEDED;
}

static void
limit_close (void *handle)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
  connections--;
}

static struct nbdkit_filter filter = {
  .name              = "limit",
  .longname          = "nbdkit limit filter",
  .config            = limit_config,
  .preconnect        = limit_preconnect,
  .open              = limit_open,
  .close             = limit_close,
};

NBDKIT_REGISTER_FILTER (filter)
