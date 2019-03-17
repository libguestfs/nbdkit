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
#include <errno.h>
#include <assert.h>

#include "internal.h"
#include "byte-swapping.h"
#include "protocol.h"

/* Maximum read or write request that we will handle. */
#define MAX_REQUEST_SIZE (64 * 1024 * 1024)

static bool
valid_range (struct connection *conn, uint64_t offset, uint32_t count)
{
  uint64_t exportsize = conn->exportsize;

  return count > 0 && offset <= exportsize && offset + count <= exportsize;
}

static bool
validate_request (struct connection *conn,
                  uint16_t cmd, uint16_t flags, uint64_t offset, uint32_t count,
                  uint32_t *error)
{
  /* Readonly connection? */
  if (conn->readonly &&
      (cmd == NBD_CMD_WRITE || cmd == NBD_CMD_TRIM ||
       cmd == NBD_CMD_WRITE_ZEROES)) {
    nbdkit_error ("invalid request: %s: write request on readonly connection",
                  name_of_nbd_cmd (cmd));
    *error = EROFS;
    return false;
  }

  /* Validate cmd, offset, count. */
  switch (cmd) {
  case NBD_CMD_READ:
  case NBD_CMD_WRITE:
  case NBD_CMD_TRIM:
  case NBD_CMD_WRITE_ZEROES:
    if (!valid_range (conn, offset, count)) {
      /* XXX Allow writes to extend the disk? */
      nbdkit_error ("invalid request: %s: offset and count are out of range: "
                    "offset=%" PRIu64 " count=%" PRIu32,
                    name_of_nbd_cmd (cmd), offset, count);
      *error = (cmd == NBD_CMD_WRITE ||
                cmd == NBD_CMD_WRITE_ZEROES) ? ENOSPC : EINVAL;
      return false;
    }
    break;

  case NBD_CMD_FLUSH:
    if (offset != 0 || count != 0) {
      nbdkit_error ("invalid request: %s: expecting offset and count = 0",
                    name_of_nbd_cmd (cmd));
      *error = EINVAL;
      return false;
    }
    break;

  default:
    nbdkit_error ("invalid request: unknown command (%" PRIu32 ") ignored",
                  cmd);
    *error = EINVAL;
    return false;
  }

  /* Validate flags */
  if (flags & ~(NBD_CMD_FLAG_FUA | NBD_CMD_FLAG_NO_HOLE)) {
    nbdkit_error ("invalid request: unknown flag (0x%x)", flags);
    *error = EINVAL;
    return false;
  }
  if ((flags & NBD_CMD_FLAG_NO_HOLE) &&
      cmd != NBD_CMD_WRITE_ZEROES) {
    nbdkit_error ("invalid request: NO_HOLE flag needs WRITE_ZEROES request");
    *error = EINVAL;
    return false;
  }
  if (!conn->can_fua && (flags & NBD_CMD_FLAG_FUA)) {
    nbdkit_error ("invalid request: FUA flag not supported");
    *error = EINVAL;
    return false;
  }

  /* Refuse over-large read and write requests. */
  if ((cmd == NBD_CMD_WRITE || cmd == NBD_CMD_READ) &&
      count > MAX_REQUEST_SIZE) {
    nbdkit_error ("invalid request: %s: data request is too large (%" PRIu32
                  " > %d)",
                  name_of_nbd_cmd (cmd), count, MAX_REQUEST_SIZE);
    *error = ENOMEM;
    return false;
  }

  /* Flush allowed? */
  if (!conn->can_flush && cmd == NBD_CMD_FLUSH) {
    nbdkit_error ("invalid request: %s: flush operation not supported",
                  name_of_nbd_cmd (cmd));
    *error = EINVAL;
    return false;
  }

  /* Trim allowed? */
  if (!conn->can_trim && cmd == NBD_CMD_TRIM) {
    nbdkit_error ("invalid request: %s: trim operation not supported",
                  name_of_nbd_cmd (cmd));
    *error = EINVAL;
    return false;
  }

  /* Zero allowed? */
  if (!conn->can_zero && cmd == NBD_CMD_WRITE_ZEROES) {
    nbdkit_error ("invalid request: %s: write zeroes operation not supported",
                  name_of_nbd_cmd (cmd));
    *error = EINVAL;
    return false;
  }

  return true;                     /* Command validates. */
}

/* This is called with the request lock held to actually execute the
 * request (by calling the plugin).  Note that the request fields have
 * been validated already in 'validate_request' so we don't have to
 * check them again.  'buf' is either the data to be written or the
 * data to be returned, and points to a buffer of size 'count' bytes.
 *
 * In all cases, the return value is the system errno value that will
 * later be converted to the nbd error to send back to the client (0
 * for success).
 */
static uint32_t
handle_request (struct connection *conn,
                uint16_t cmd, uint16_t flags, uint64_t offset, uint32_t count,
                void *buf)
{
  uint32_t f = 0;
  bool fua = conn->can_fua && (flags & NBD_CMD_FLAG_FUA);
  int err = 0;

  /* Clear the error, so that we know if the plugin calls
   * nbdkit_set_error() or relied on errno.  */
  threadlocal_set_error (0);

  switch (cmd) {
  case NBD_CMD_READ:
    if (backend->pread (backend, conn, buf, count, offset, 0, &err) == -1)
      return err;
    break;

  case NBD_CMD_WRITE:
    if (fua)
      f |= NBDKIT_FLAG_FUA;
    if (backend->pwrite (backend, conn, buf, count, offset, f, &err) == -1)
      return err;
    break;

  case NBD_CMD_FLUSH:
    if (backend->flush (backend, conn, 0, &err) == -1)
      return err;
    break;

  case NBD_CMD_TRIM:
    if (fua)
      f |= NBDKIT_FLAG_FUA;
    if (backend->trim (backend, conn, count, offset, f, &err) == -1)
      return err;
    break;

  case NBD_CMD_WRITE_ZEROES:
    if (!(flags & NBD_CMD_FLAG_NO_HOLE))
      f |= NBDKIT_FLAG_MAY_TRIM;
    if (fua)
      f |= NBDKIT_FLAG_FUA;
    if (backend->zero (backend, conn, count, offset, f, &err) == -1)
      return err;
    break;

  default:
    abort ();
  }

  return 0;
}

static int
skip_over_write_buffer (int sock, size_t count)
{
  char buf[BUFSIZ];
  ssize_t r;

  if (count > MAX_REQUEST_SIZE * 2) {
    nbdkit_error ("write request too large to skip");
    return -1;
  }

  while (count > 0) {
    r = read (sock, buf, count > BUFSIZ ? BUFSIZ : count);
    if (r == -1) {
      nbdkit_error ("skipping write buffer: %m");
      return -1;
    }
    if (r == 0)  {
      nbdkit_error ("unexpected early EOF");
      errno = EBADMSG;
      return -1;
    }
    count -= r;
  }
  return 0;
}

/* Convert a system errno to an NBD_E* error code. */
static int
nbd_errno (int error)
{
  switch (error) {
  case 0:
    return NBD_SUCCESS;
  case EROFS:
  case EPERM:
    return NBD_EPERM;
  case EIO:
    return NBD_EIO;
  case ENOMEM:
    return NBD_ENOMEM;
#ifdef EDQUOT
  case EDQUOT:
#endif
  case EFBIG:
  case ENOSPC:
    return NBD_ENOSPC;
#ifdef ESHUTDOWN
  case ESHUTDOWN:
    return NBD_ESHUTDOWN;
#endif
  case EINVAL:
  default:
    return NBD_EINVAL;
  }
}

static int
send_simple_reply (struct connection *conn,
                   uint64_t handle, uint16_t cmd,
                   const char *buf, uint32_t count,
                   uint32_t error)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&conn->write_lock);
  struct simple_reply reply;
  int r;

  reply.magic = htobe32 (NBD_SIMPLE_REPLY_MAGIC);
  reply.handle = handle;
  reply.error = htobe32 (nbd_errno (error));

  r = conn->send (conn, &reply, sizeof reply);
  if (r == -1) {
    nbdkit_error ("write reply: %s: %m", name_of_nbd_cmd (cmd));
    return connection_set_status (conn, -1);
  }

  /* Send the read data buffer. */
  if (cmd == NBD_CMD_READ && !error) {
    r = conn->send (conn, buf, count);
    if (r == -1) {
      nbdkit_error ("write data: %s: %m", name_of_nbd_cmd (cmd));
      return connection_set_status (conn, -1);
    }
  }

  return 1;                     /* command processed ok */
}

static int
send_structured_reply_read (struct connection *conn,
                            uint64_t handle, uint16_t cmd,
                            const char *buf, uint32_t count, uint64_t offset)
{
  /* Once we are really using structured replies and sending data back
   * in chunks, we'll be able to grab the write lock for each chunk,
   * allowing other threads to interleave replies.  As we're not doing
   * that yet we acquire the lock for the whole function.
   */
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&conn->write_lock);
  struct structured_reply reply;
  struct structured_reply_offset_data offset_data;
  int r;

  assert (cmd == NBD_CMD_READ);

  reply.magic = htobe32 (NBD_STRUCTURED_REPLY_MAGIC);
  reply.handle = handle;
  reply.flags = htobe16 (NBD_REPLY_FLAG_DONE);
  reply.type = htobe16 (NBD_REPLY_TYPE_OFFSET_DATA);
  reply.length = htobe32 (count + sizeof offset_data);

  r = conn->send (conn, &reply, sizeof reply);
  if (r == -1) {
    nbdkit_error ("write reply: %s: %m", name_of_nbd_cmd (cmd));
    return connection_set_status (conn, -1);
  }

  /* Send the offset + read data buffer. */
  offset_data.offset = htobe64 (offset);
  r = conn->send (conn, &offset_data, sizeof offset_data);
  if (r == -1) {
    nbdkit_error ("write data: %s: %m", name_of_nbd_cmd (cmd));
    return connection_set_status (conn, -1);
  }

  r = conn->send (conn, buf, count);
  if (r == -1) {
    nbdkit_error ("write data: %s: %m", name_of_nbd_cmd (cmd));
    return connection_set_status (conn, -1);
  }

  return 1;                     /* command processed ok */
}

static int
send_structured_reply_error (struct connection *conn,
                             uint64_t handle, uint16_t cmd, uint32_t error)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&conn->write_lock);
  struct structured_reply reply;
  struct structured_reply_error error_data;
  int r;

  reply.magic = htobe32 (NBD_STRUCTURED_REPLY_MAGIC);
  reply.handle = handle;
  reply.flags = htobe16 (NBD_REPLY_FLAG_DONE);
  reply.type = htobe16 (NBD_REPLY_TYPE_ERROR);
  reply.length = htobe32 (0 /* no human readable error */ + sizeof error_data);

  r = conn->send (conn, &reply, sizeof reply);
  if (r == -1) {
    nbdkit_error ("write error reply: %m");
    return connection_set_status (conn, -1);
  }

  /* Send the error. */
  error_data.error = htobe32 (error);
  error_data.len = htobe16 (0);
  r = conn->send (conn, &error_data, sizeof error_data);
  if (r == -1) {
    nbdkit_error ("write data: %s: %m", name_of_nbd_cmd (cmd));
    return connection_set_status (conn, -1);
  }
  /* No human readable error message at the moment. */

  return 1;                     /* command processed ok */
}

int
protocol_recv_request_send_reply (struct connection *conn)
{
  int r;
  struct request request;
  uint16_t cmd, flags;
  uint32_t magic, count, error = 0;
  uint64_t offset;
  CLEANUP_FREE char *buf = NULL;

  /* Read the request packet. */
  {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&conn->read_lock);
    r = connection_get_status (conn);
    if (r <= 0)
      return r;
    r = conn->recv (conn, &request, sizeof request);
    if (r == -1) {
      nbdkit_error ("read request: %m");
      return connection_set_status (conn, -1);
    }
    if (r == 0) {
      debug ("client closed input socket, closing connection");
      return connection_set_status (conn, 0); /* disconnect */
    }

    magic = be32toh (request.magic);
    if (magic != NBD_REQUEST_MAGIC) {
      nbdkit_error ("invalid request: 'magic' field is incorrect (0x%x)",
                    magic);
      return connection_set_status (conn, -1);
    }

    flags = be16toh (request.flags);
    cmd = be16toh (request.type);

    offset = be64toh (request.offset);
    count = be32toh (request.count);

    if (cmd == NBD_CMD_DISC) {
      debug ("client sent %s, closing connection", name_of_nbd_cmd (cmd));
      return connection_set_status (conn, 0); /* disconnect */
    }

    /* Validate the request. */
    if (!validate_request (conn, cmd, flags, offset, count, &error)) {
      if (cmd == NBD_CMD_WRITE &&
          skip_over_write_buffer (conn->sockin, count) < 0)
        return connection_set_status (conn, -1);
      goto send_reply;
    }

    /* Allocate the data buffer used for either read or write requests. */
    if (cmd == NBD_CMD_READ || cmd == NBD_CMD_WRITE) {
      buf = malloc (count);
      if (buf == NULL) {
        perror ("malloc");
        error = ENOMEM;
        if (cmd == NBD_CMD_WRITE &&
            skip_over_write_buffer (conn->sockin, count) < 0)
          return connection_set_status (conn, -1);
        goto send_reply;
      }
    }

    /* Receive the write data buffer. */
    if (cmd == NBD_CMD_WRITE) {
      r = conn->recv (conn, buf, count);
      if (r == 0) {
        errno = EBADMSG;
        r = -1;
      }
      if (r == -1) {
        nbdkit_error ("read data: %s: %m", name_of_nbd_cmd (cmd));
        return connection_set_status (conn, -1);
      }
    }
  }

  /* Perform the request.  Only this part happens inside the request lock. */
  if (quit || !connection_get_status (conn)) {
    error = ESHUTDOWN;
  }
  else {
    lock_request (conn);
    error = handle_request (conn, cmd, flags, offset, count, buf);
    assert ((int) error >= 0);
    unlock_request (conn);
  }

  /* Send the reply packet. */
 send_reply:
  if (connection_get_status (conn) < 0)
    return -1;

  if (error != 0) {
    /* Since we're about to send only the limited NBD_E* errno to the
     * client, don't lose the information about what really happened
     * on the server side.  Make sure there is a way for the operator
     * to retrieve the real error.
     */
    debug ("sending error reply: %s", strerror (error));
  }

  /* Currently we prefer to send simple replies for everything except
   * where we have to (ie. NBD_CMD_READ when structured_replies have
   * been negotiated).  However this prevents us from sending
   * human-readable error messages to the client, so we should
   * reconsider this in future.
   */
  if (conn->structured_replies && cmd == NBD_CMD_READ) {
    if (!error)
      return send_structured_reply_read (conn, request.handle, cmd,
                                         buf, count, offset);
    else
      return send_structured_reply_error (conn, request.handle, cmd, error);
  }
  else
    return send_simple_reply (conn, request.handle, cmd, buf, count, error);
}
