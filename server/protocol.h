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

#ifndef NBDKIT_PROTOCOL_H
#define NBDKIT_PROTOCOL_H

#include <stdint.h>

/* Note that all NBD fields are sent on the wire in network byte
 * order, so we must use beXXtoh or htobeXX when reading or writing
 * these structures.
 */

/* Old-style handshake. */
struct old_handshake {
  char nbdmagic[8];           /* "NBDMAGIC" */
  uint64_t version;           /* OLD_VERSION */
  uint64_t exportsize;
  uint16_t gflags;            /* global flags */
  uint16_t eflags;            /* per-export flags */
  char zeroes[124];           /* must be sent as zero bytes */
} __attribute__((packed));

#define OLD_VERSION UINT64_C(0x420281861253)

/* New-style handshake. */
struct new_handshake {
  char nbdmagic[8];           /* "NBDMAGIC" */
  uint64_t version;           /* NEW_VERSION */
  uint16_t gflags;            /* global flags */
} __attribute__((packed));

#define NEW_VERSION UINT64_C(0x49484156454F5054)

/* New-style handshake option (sent by the client to us). */
struct new_option {
  uint64_t version;           /* NEW_VERSION */
  uint32_t option;            /* NBD_OPT_* */
  uint32_t optlen;            /* option data length */
  /* option data follows */
} __attribute__((packed));

/* Fixed newstyle handshake reply message. */
struct fixed_new_option_reply {
  uint64_t magic;             /* NBD_REP_MAGIC */
  uint32_t option;            /* option we are replying to */
  uint32_t reply;             /* NBD_REP_* */
  uint32_t replylen;
} __attribute__((packed));

#define NBD_REP_MAGIC UINT64_C(0x3e889045565a9)

/* Global flags. */
extern const char *name_of_nbd_global_flag (int);
#define NBD_FLAG_FIXED_NEWSTYLE 1
#define NBD_FLAG_NO_ZEROES      2

/* Per-export flags. */
extern const char *name_of_nbd_flag (int);
#define NBD_FLAG_HAS_FLAGS         (1 << 0)
#define NBD_FLAG_READ_ONLY         (1 << 1)
#define NBD_FLAG_SEND_FLUSH        (1 << 2)
#define NBD_FLAG_SEND_FUA          (1 << 3)
#define NBD_FLAG_ROTATIONAL        (1 << 4)
#define NBD_FLAG_SEND_TRIM         (1 << 5)
#define NBD_FLAG_SEND_WRITE_ZEROES (1 << 6)
#define NBD_FLAG_CAN_MULTI_CONN    (1 << 8)

/* NBD options (new style handshake only). */
extern const char *name_of_nbd_opt (int);
#define NBD_OPT_EXPORT_NAME        1
#define NBD_OPT_ABORT              2
#define NBD_OPT_LIST               3
#define NBD_OPT_STARTTLS           5
#define NBD_OPT_INFO               6
#define NBD_OPT_GO                 7
#define NBD_OPT_STRUCTURED_REPLY   8

extern const char *name_of_nbd_rep (int);
#define NBD_REP_ACK          1
#define NBD_REP_SERVER       2
#define NBD_REP_INFO         3
#define NBD_REP_ERR_UNSUP    0x80000001
#define NBD_REP_ERR_POLICY   0x80000002
#define NBD_REP_ERR_INVALID  0x80000003
#define NBD_REP_ERR_PLATFORM 0x80000004
#define NBD_REP_ERR_TLS_REQD 0x80000005

extern const char *name_of_nbd_info (int);
#define NBD_INFO_EXPORT      0

/* NBD_INFO_EXPORT reply (follows fixed_new_option_reply). */
struct fixed_new_option_reply_info_export {
  uint16_t info;                /* NBD_INFO_EXPORT */
  uint64_t exportsize;          /* size of export */
  uint16_t eflags;              /* per-export flags */
} __attribute__((packed));

/* New-style handshake server reply when using NBD_OPT_EXPORT_NAME.
 * Modern clients use NBD_OPT_GO instead of this.
 */
struct new_handshake_finish {
  uint64_t exportsize;
  uint16_t eflags;            /* per-export flags */
  char zeroes[124];           /* must be sent as zero bytes */
} __attribute__((packed));

/* Request (client -> server). */
struct request {
  uint32_t magic;               /* NBD_REQUEST_MAGIC. */
  uint16_t flags;               /* Request flags. */
  uint16_t type;                /* Request type. */
  uint64_t handle;              /* Opaque handle. */
  uint64_t offset;              /* Request offset. */
  uint32_t count;               /* Request length. */
} __attribute__((packed));

/* Simple reply (server -> client). */
struct simple_reply {
  uint32_t magic;               /* NBD_SIMPLE_REPLY_MAGIC. */
  uint32_t error;               /* NBD_SUCCESS or one of NBD_E*. */
  uint64_t handle;              /* Opaque handle. */
} __attribute__((packed));

/* Structured reply (server -> client). */
struct structured_reply {
  uint32_t magic;               /* NBD_STRUCTURED_REPLY_MAGIC. */
  uint16_t flags;               /* NBD_REPLY_FLAG_* */
  uint16_t type;                /* NBD_REPLY_TYPE_* */
  uint64_t handle;              /* Opaque handle. */
  uint32_t length;              /* Length of payload which follows. */
} __attribute__((packed));

struct structured_reply_offset_data {
  uint64_t offset;              /* offset */
  /* Followed by data. */
} __attribute__((packed));

struct structured_reply_error {
  uint32_t error;               /* NBD_E* error number */
  uint16_t len;                 /* Length of human readable error. */
  /* Followed by human readable error string. */
} __attribute__((packed));

#define NBD_REQUEST_MAGIC           0x25609513
#define NBD_SIMPLE_REPLY_MAGIC      0x67446698
#define NBD_STRUCTURED_REPLY_MAGIC  0x668e33ef

/* Structured reply flags. */
extern const char *name_of_nbd_reply_flag (int);
#define NBD_REPLY_FLAG_DONE         (1<<0)

/* Structured reply types. */
extern const char *name_of_nbd_reply_type (int);
#define NBD_REPLY_TYPE_NONE         0
#define NBD_REPLY_TYPE_OFFSET_DATA  1
#define NBD_REPLY_TYPE_OFFSET_HOLE  2
#define NBD_REPLY_TYPE_BLOCK_STATUS 3
#define NBD_REPLY_TYPE_ERROR        ((1<<15) + 1)
#define NBD_REPLY_TYPE_ERROR_OFFSET ((1<<15) + 2)

/* NBD commands. */
extern const char *name_of_nbd_cmd (int);
#define NBD_CMD_READ              0
#define NBD_CMD_WRITE             1
#define NBD_CMD_DISC              2 /* Disconnect. */
#define NBD_CMD_FLUSH             3
#define NBD_CMD_TRIM              4
#define NBD_CMD_WRITE_ZEROES      6

extern const char *name_of_nbd_cmd_flag (int);
#define NBD_CMD_FLAG_FUA      (1<<0)
#define NBD_CMD_FLAG_NO_HOLE  (1<<1)

/* Error codes (previously errno).
 * See http://git.qemu.org/?p=qemu.git;a=commitdiff;h=ca4414804114fd0095b317785bc0b51862e62ebb
 */
extern const char *name_of_nbd_error (int);
#define NBD_SUCCESS     0
#define NBD_EPERM       1
#define NBD_EIO         5
#define NBD_ENOMEM     12
#define NBD_EINVAL     22
#define NBD_ENOSPC     28
#define NBD_ESHUTDOWN 108

#endif /* NBDKIT_PROTOCOL_H */
