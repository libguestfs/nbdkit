/* nbdkit
 * Copyright (C) 2013-2019 Red Hat Inc.
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
#include "protocol.h"

/* eflags calculation is the same between oldstyle and newstyle
 * protocols.
 */
int
protocol_compute_eflags (struct connection *conn, uint16_t *flags)
{
  uint16_t eflags = NBD_FLAG_HAS_FLAGS;
  int fl;

  /* Check all flags even if they won't be advertised, to prime the
   * cache and make later request validation easier.
   */
  fl = backend_can_write (backend, conn);
  if (fl == -1)
    return -1;
  if (!fl)
    eflags |= NBD_FLAG_READ_ONLY;

  fl = backend_can_zero (backend, conn);
  if (fl == -1)
    return -1;
  if (fl)
    eflags |= NBD_FLAG_SEND_WRITE_ZEROES;

  fl = backend_can_fast_zero (backend, conn);
  if (fl == -1)
    return -1;
  if (fl)
    eflags |= NBD_FLAG_SEND_FAST_ZERO;

  fl = backend_can_trim (backend, conn);
  if (fl == -1)
    return -1;
  if (fl)
    eflags |= NBD_FLAG_SEND_TRIM;

  fl = backend_can_fua (backend, conn);
  if (fl == -1)
    return -1;
  if (fl)
    eflags |= NBD_FLAG_SEND_FUA;

  fl = backend_can_flush (backend, conn);
  if (fl == -1)
    return -1;
  if (fl)
    eflags |= NBD_FLAG_SEND_FLUSH;

  fl = backend_is_rotational (backend, conn);
  if (fl == -1)
    return -1;
  if (fl)
    eflags |= NBD_FLAG_ROTATIONAL;

  /* multi-conn is useless if parallel connections are not allowed. */
  fl = backend_can_multi_conn (backend, conn);
  if (fl == -1)
    return -1;
  if (fl && (backend->thread_model (backend) >
             NBDKIT_THREAD_MODEL_SERIALIZE_CONNECTIONS))
    eflags |= NBD_FLAG_CAN_MULTI_CONN;

  fl = backend_can_cache (backend, conn);
  if (fl == -1)
    return -1;
  if (fl)
    eflags |= NBD_FLAG_SEND_CACHE;

  /* The result of this is not directly advertised as part of the
   * handshake, but priming the cache here makes BLOCK_STATUS handling
   * not have to worry about errors, and makes test-layers easier to
   * write.
   */
  fl = backend_can_extents (backend, conn);
  if (fl == -1)
    return -1;

  if (conn->structured_replies)
    eflags |= NBD_FLAG_SEND_DF;

  *flags = eflags;
  return 0;
}

int
protocol_handshake (struct connection *conn)
{
  int r;

  lock_request (conn);
  if (!newstyle)
    r = protocol_handshake_oldstyle (conn);
  else
    r = protocol_handshake_newstyle (conn);
  unlock_request (conn);

  return r;
}
