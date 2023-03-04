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

#include <nbdkit-filter.h>

#ifdef HAVE_STDATOMIC_H
#include <stdatomic.h>
#else
/* Some old platforms lack atomic types, but 32 bit ints are usually
 * "atomic enough".
 */
#define _Atomic /**/
#endif

/* Counts client connections.  When this drops to zero we exit. */
static _Atomic unsigned connections;

static void *
exitlast_open (nbdkit_next_open *next, nbdkit_context *nxdata,
               int readonly, const char *exportname, int is_tls)
{
  if (next (nxdata, readonly, exportname) == -1)
    return NULL;

  connections++;

  return NBDKIT_HANDLE_NOT_NEEDED;
}

static void
exitlast_close (void *handle)
{
  if (--connections == 0) {
    nbdkit_debug ("exitlast: exiting on last client connection");
    nbdkit_shutdown ();
  }
}

static struct nbdkit_filter filter = {
  .name              = "exitlast",
  .longname          = "nbdkit exitlast filter",
  .open              = exitlast_open,
  .close             = exitlast_close,
};

NBDKIT_REGISTER_FILTER (filter)
