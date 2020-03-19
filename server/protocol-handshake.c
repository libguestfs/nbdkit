/* nbdkit
 * Copyright (C) 2013-2020 Red Hat Inc.
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
#include <unistd.h>

#include "internal.h"
#include "byte-swapping.h"
#include "nbd-protocol.h"

int
protocol_handshake ()
{
  int r;

  lock_request ();
  if (!newstyle)
    r = protocol_handshake_oldstyle ();
  else
    r = protocol_handshake_newstyle ();
  unlock_request ();

  return r;
}

/* Common code used by oldstyle and newstyle protocols to:
 *
 * - call the backend .open method
 *
 * - get the export size
 *
 * - compute the eflags (same between oldstyle and newstyle
 *   protocols)
 *
 * The protocols must defer this as late as possible so that
 * unauthorized clients can't cause unnecessary work in .open by
 * simply opening a TCP connection.
 */
int
protocol_common_open (uint64_t *exportsize, uint16_t *flags)
{
  GET_CONN;
  int64_t size;
  uint16_t eflags = NBD_FLAG_HAS_FLAGS;
  int fl;

  if (backend_open (top, read_only) == -1)
    return -1;

  /* Prepare (for filters), called just after open. */
  if (backend_prepare (top) == -1)
    return -1;

  size = backend_get_size (top);
  if (size == -1)
    return -1;
  if (size < 0) {
    nbdkit_error (".get_size function returned invalid value "
                  "(%" PRIi64 ")", size);
    return -1;
  }

  /* Check all flags even if they won't be advertised, to prime the
   * cache and make later request validation easier.
   */
  fl = backend_can_write (top);
  if (fl == -1)
    return -1;
  if (!fl)
    eflags |= NBD_FLAG_READ_ONLY;

  fl = backend_can_zero (top);
  if (fl == -1)
    return -1;
  if (fl)
    eflags |= NBD_FLAG_SEND_WRITE_ZEROES;

  fl = backend_can_fast_zero (top);
  if (fl == -1)
    return -1;
  if (fl)
    eflags |= NBD_FLAG_SEND_FAST_ZERO;

  fl = backend_can_trim (top);
  if (fl == -1)
    return -1;
  if (fl)
    eflags |= NBD_FLAG_SEND_TRIM;

  fl = backend_can_fua (top);
  if (fl == -1)
    return -1;
  if (fl)
    eflags |= NBD_FLAG_SEND_FUA;

  fl = backend_can_flush (top);
  if (fl == -1)
    return -1;
  if (fl)
    eflags |= NBD_FLAG_SEND_FLUSH;

  fl = backend_is_rotational (top);
  if (fl == -1)
    return -1;
  if (fl)
    eflags |= NBD_FLAG_ROTATIONAL;

  /* multi-conn is useless if parallel connections are not allowed. */
  fl = backend_can_multi_conn (top);
  if (fl == -1)
    return -1;
  if (fl && (thread_model > NBDKIT_THREAD_MODEL_SERIALIZE_CONNECTIONS))
    eflags |= NBD_FLAG_CAN_MULTI_CONN;

  fl = backend_can_cache (top);
  if (fl == -1)
    return -1;
  if (fl)
    eflags |= NBD_FLAG_SEND_CACHE;

  /* The result of this is not directly advertised as part of the
   * handshake, but priming the cache here makes BLOCK_STATUS handling
   * not have to worry about errors, and makes test-layers easier to
   * write.
   */
  fl = backend_can_extents (top);
  if (fl == -1)
    return -1;

  if (conn->structured_replies)
    eflags |= NBD_FLAG_SEND_DF;

  *exportsize = size;
  *flags = eflags;
  return 0;
}
