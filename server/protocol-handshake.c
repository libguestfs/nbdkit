/* nbdkit
 * Copyright (C) 2013-2019 Red Hat Inc.
 * All rights reserved.
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

  fl = backend->can_write (backend, conn);
  if (fl == -1)
    return -1;
  if (readonly || !fl) {
    eflags |= NBD_FLAG_READ_ONLY;
    conn->readonly = true;
  }
  if (!conn->readonly) {
    fl = backend->can_zero (backend, conn);
    if (fl == -1)
      return -1;
    if (fl) {
      eflags |= NBD_FLAG_SEND_WRITE_ZEROES;
      conn->can_zero = true;
    }

    fl = backend->can_trim (backend, conn);
    if (fl == -1)
      return -1;
    if (fl) {
      eflags |= NBD_FLAG_SEND_TRIM;
      conn->can_trim = true;
    }

    fl = backend->can_fua (backend, conn);
    if (fl == -1)
      return -1;
    if (fl) {
      eflags |= NBD_FLAG_SEND_FUA;
      conn->can_fua = true;
    }
  }

  fl = backend->can_flush (backend, conn);
  if (fl == -1)
    return -1;
  if (fl) {
    eflags |= NBD_FLAG_SEND_FLUSH;
    conn->can_flush = true;
  }

  fl = backend->is_rotational (backend, conn);
  if (fl == -1)
    return -1;
  if (fl) {
    eflags |= NBD_FLAG_ROTATIONAL;
    conn->is_rotational = true;
  }

  fl = backend->can_multi_conn (backend, conn);
  if (fl == -1)
    return -1;
  if (fl) {
    eflags |= NBD_FLAG_CAN_MULTI_CONN;
    conn->can_multi_conn = true;
  }

  /* The result of this is not returned to callers here (or at any
   * time during the handshake).  However it makes sense to do it once
   * per connection and store the result in the handle anyway.  This
   * protocol_compute_eflags function is a bit misnamed XXX.
   */
  fl = backend->can_extents (backend, conn);
  if (fl == -1)
    return -1;
  if (fl)
    conn->can_extents = true;

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
