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
#include "protostrings.h"

/* Maximum number of client options we allow before giving up. */
#define MAX_NR_OPTIONS 32

/* Receive newstyle options. */
static int
send_newstyle_option_reply (uint32_t option, uint32_t reply)
{
  GET_CONN;
  struct nbd_fixed_new_option_reply fixed_new_option_reply;

  fixed_new_option_reply.magic = htobe64 (NBD_REP_MAGIC);
  fixed_new_option_reply.option = htobe32 (option);
  fixed_new_option_reply.reply = htobe32 (reply);
  fixed_new_option_reply.replylen = htobe32 (0);

  if (conn->send (&fixed_new_option_reply,
                  sizeof fixed_new_option_reply, 0) == -1) {
    /* The protocol document says that the client is allowed to simply
     * drop the connection after sending NBD_OPT_ABORT, or may read
     * the reply.
     */
    if (option == NBD_OPT_ABORT)
      debug ("write: %s: %m", name_of_nbd_opt (option));
    else
      nbdkit_error ("write: %s: %m", name_of_nbd_opt (option));
    return -1;
  }

  return 0;
}

/* Reply to NBD_OPT_LIST with the plugin's list of export names.
 */
static int
send_newstyle_option_reply_exportnames (uint32_t option)
{
  GET_CONN;
  struct nbd_fixed_new_option_reply fixed_new_option_reply;
  size_t i;
  CLEANUP_EXPORTS_FREE struct nbdkit_exports *exps = NULL;

  exps = nbdkit_exports_new (false);
  if (exps == NULL)
    return send_newstyle_option_reply (option, NBD_REP_ERR_TOO_BIG);
  if (backend_list_exports (top, read_only, false, exps) == -1)
    return send_newstyle_option_reply (option, NBD_REP_ERR_PLATFORM);

  for (i = 0; i < nbdkit_exports_count (exps); i++) {
    const struct nbdkit_export export = nbdkit_get_export (exps, i);
    size_t name_len = strlen (export.name);
    size_t desc_len = export.description ? strlen (export.description) : 0;
    uint32_t len;

    fixed_new_option_reply.magic = htobe64 (NBD_REP_MAGIC);
    fixed_new_option_reply.option = htobe32 (option);
    fixed_new_option_reply.reply = htobe32 (NBD_REP_SERVER);
    fixed_new_option_reply.replylen = htobe32 (name_len + sizeof (len) +
                                               desc_len);

    if (conn->send (&fixed_new_option_reply,
                    sizeof fixed_new_option_reply, SEND_MORE) == -1) {
      nbdkit_error ("write: %s: %m", name_of_nbd_opt (option));
      return -1;
    }

    len = htobe32 (name_len);
    if (conn->send (&len, sizeof len, SEND_MORE) == -1) {
      nbdkit_error ("write: %s: %s: %m",
                    name_of_nbd_opt (option), "sending length");
      return -1;
    }
    if (conn->send (export.name, name_len, SEND_MORE) == -1) {
      nbdkit_error ("write: %s: %s: %m",
                    name_of_nbd_opt (option), "sending export name");
      return -1;
    }
    if (conn->send (export.description, desc_len, 0) == -1) {
      nbdkit_error ("write: %s: %s: %m",
                    name_of_nbd_opt (option), "sending export description");
      return -1;
    }
  }

  return send_newstyle_option_reply (option, NBD_REP_ACK);
}

static int
send_newstyle_option_reply_info_export (uint32_t option, uint32_t reply,
                                        uint16_t info, uint64_t exportsize)
{
  GET_CONN;
  struct nbd_fixed_new_option_reply fixed_new_option_reply;
  struct nbd_fixed_new_option_reply_info_export export;

  fixed_new_option_reply.magic = htobe64 (NBD_REP_MAGIC);
  fixed_new_option_reply.option = htobe32 (option);
  fixed_new_option_reply.reply = htobe32 (reply);
  fixed_new_option_reply.replylen = htobe32 (sizeof export);
  export.info = htobe16 (info);
  export.exportsize = htobe64 (exportsize);
  export.eflags = htobe16 (conn->eflags);

  if (conn->send (&fixed_new_option_reply,
                  sizeof fixed_new_option_reply, SEND_MORE) == -1 ||
      conn->send (&export, sizeof export, 0) == -1) {
    nbdkit_error ("write: %s: %m", name_of_nbd_opt (option));
    return -1;
  }

  return 0;
}

static int
send_newstyle_option_reply_meta_context (uint32_t option, uint32_t reply,
                                         uint32_t context_id,
                                         const char *name)
{
  GET_CONN;
  struct nbd_fixed_new_option_reply fixed_new_option_reply;
  struct nbd_fixed_new_option_reply_meta_context context;
  const size_t namelen = strlen (name);

  debug ("newstyle negotiation: %s: replying with %s id %d",
         name_of_nbd_opt (option), name, context_id);
  fixed_new_option_reply.magic = htobe64 (NBD_REP_MAGIC);
  fixed_new_option_reply.option = htobe32 (option);
  fixed_new_option_reply.reply = htobe32 (reply);
  fixed_new_option_reply.replylen = htobe32 (sizeof context + namelen);
  context.context_id = htobe32 (context_id);

  if (conn->send (&fixed_new_option_reply,
                  sizeof fixed_new_option_reply, SEND_MORE) == -1 ||
      conn->send (&context, sizeof context, SEND_MORE) == -1 ||
      conn->send (name, namelen, 0) == -1) {
    nbdkit_error ("write: %s: %m", name_of_nbd_opt (option));
    return -1;
  }

  return 0;
}

/* Sub-function during negotiate_handshake_newstyle, to uniformly handle
 * a client hanging up on a message boundary.
 */
static int __attribute__ ((format (printf, 3, 4)))
conn_recv_full (void *buf, size_t len, const char *fmt, ...)
{
  GET_CONN;
  int r = conn->recv (buf, len);
  va_list args;

  if (r == -1) {
    va_start (args, fmt);
    nbdkit_verror (fmt, args);
    va_end (args);
    return -1;
  }
  if (r == 0) {
    /* During negotiation, client EOF on message boundary is less
     * severe than failure in the middle of the buffer. */
    debug ("client closed input socket, closing connection");
    return -1;
  }
  return r;
}

/* Sub-function of negotiate_handshake_newstyle_options below.  It
 * must be called on all non-error paths out of the options for-loop
 * in that function, and must not cause any wire traffic.
 */
static int
finish_newstyle_options (uint64_t *exportsize,
                         const char *exportname_in, uint32_t exportnamelen)
{
  GET_CONN;

  /* Since the exportname string passed here comes directly out of the
   * NBD protocol make a temporary copy of the exportname into a
   * \0-terminated buffer.
   */
  CLEANUP_FREE char *exportname = strndup (exportname_in, exportnamelen);
  if (exportname == NULL) {
    nbdkit_error ("strndup: %m");
    return -1;
  }

  /* The NBD spec says that if the client later uses NBD_OPT_GO on a
   * different export, then the context from the earlier
   * NBD_OPT_SET_META_CONTEXT is not usable so discard it.
   */
  if (conn->exportname_from_set_meta_context &&
      strcmp (conn->exportname_from_set_meta_context, exportname) != 0) {
    debug ("newstyle negotiation: NBD_OPT_SET_META_CONTEXT export name \"%s\" "
           "≠ final client exportname \"%s\", "
           "so discarding the previous context",
           conn->exportname_from_set_meta_context, exportname);
    conn->meta_context_base_allocation = false;
  }

  if (protocol_common_open (exportsize, &conn->eflags, exportname) == -1)
    return -1;

  debug ("newstyle negotiation: flags: export 0x%x", conn->eflags);
  return 0;
}

/* Check that the string sent as part of @option, beginning at @buf,
 * and with a client-reported length of @len, fits within @maxlen
 * bytes and is well-formed.  If not, report an error mentioning
 * @name.
 */
static int
check_string (uint32_t option, char *buf, uint32_t len, uint32_t maxlen,
              const char *name)
{
  if (len > NBD_MAX_STRING || len > maxlen) {
    nbdkit_error ("%s: %s too long", name_of_nbd_opt (option), name);
    return -1;
  }
  if (strnlen (buf, len) != len) {
    nbdkit_error ("%s: %s may not include NUL bytes",
                  name_of_nbd_opt (option), name);
    return -1;
  }
  /* TODO: Check for valid UTF-8? */
  return 0;
}

/* Sub-function of negotiate_handshake_newstyle_options, to grab and
 * validate an export name.
 */
static int
check_export_name (uint32_t option, char *buf,
                   uint32_t exportnamelen, uint32_t maxlen)
{
  GET_CONN;

  if (check_string (option, buf, exportnamelen, maxlen, "export name") == -1)
    return -1;

  debug ("newstyle negotiation: %s: client requested export '%.*s'",
         name_of_nbd_opt (option), (int) exportnamelen, buf);
  return 0;
}

static int
negotiate_handshake_newstyle_options (void)
{
  GET_CONN;
  struct nbd_new_option new_option;
  size_t nr_options;
  uint64_t version;
  uint32_t option;
  uint32_t optlen;
  struct nbd_export_name_option_reply handshake_finish;
  const char *optname;
  uint64_t exportsize;

  for (nr_options = 0; nr_options < MAX_NR_OPTIONS; ++nr_options) {
    CLEANUP_FREE char *data = NULL;

    if (conn_recv_full (&new_option, sizeof new_option,
                        "reading option: conn->recv: %m") == -1)
      return -1;

    version = be64toh (new_option.version);
    if (version != NBD_NEW_VERSION) {
      nbdkit_error ("unknown option version %" PRIx64
                    ", expecting %" PRIx64,
                    version, NBD_NEW_VERSION);
      return -1;
    }

    /* There is a maximum option length we will accept, regardless
     * of the option type.
     */
    optlen = be32toh (new_option.optlen);
    if (optlen > MAX_REQUEST_SIZE) {
      nbdkit_error ("client option data too long (%" PRIu32 ")", optlen);
      return -1;
    }
    data = malloc (optlen + 1); /* Allowing a trailing NUL helps some uses */
    if (data == NULL) {
      nbdkit_error ("malloc: %m");
      return -1;
    }

    option = be32toh (new_option.option);
    optname = name_of_nbd_opt (option);

    /* If the client lacks fixed newstyle support, it should only send
     * NBD_OPT_EXPORT_NAME.
     */
    if (!(conn->cflags & NBD_FLAG_FIXED_NEWSTYLE) &&
        option != NBD_OPT_EXPORT_NAME) {
      if (send_newstyle_option_reply (option, NBD_REP_ERR_INVALID))
        return -1;
      continue;
    }

    /* In --tls=require / FORCEDTLS mode the only options allowed
     * before TLS negotiation are NBD_OPT_ABORT and NBD_OPT_STARTTLS.
     */
    if (tls == 2 && !conn->using_tls &&
        !(option == NBD_OPT_ABORT || option == NBD_OPT_STARTTLS)) {
      if (send_newstyle_option_reply (option, NBD_REP_ERR_TLS_REQD))
        return -1;
      continue;
    }

    switch (option) {
    case NBD_OPT_EXPORT_NAME:
      if (conn_recv_full (data, optlen,
                          "read: %s: %m", name_of_nbd_opt (option)) == -1)
        return -1;
      if (check_export_name (option, data, optlen, optlen) == -1)
        return -1;

      /* We have to finish the handshake by sending handshake_finish.
       * On failure, we have to disconnect.
       */
      if (finish_newstyle_options (&exportsize, data, optlen) == -1)
        return -1;

      memset (&handshake_finish, 0, sizeof handshake_finish);
      handshake_finish.exportsize = htobe64 (exportsize);
      handshake_finish.eflags = htobe16 (conn->eflags);

      if (conn->send (&handshake_finish,
                      (conn->cflags & NBD_FLAG_NO_ZEROES)
                      ? offsetof (struct nbd_export_name_option_reply, zeroes)
                      : sizeof handshake_finish, 0) == -1) {
        nbdkit_error ("write: %s: %m", optname);
        return -1;
      }
      break;

    case NBD_OPT_ABORT:
      if (send_newstyle_option_reply (option, NBD_REP_ACK) == -1)
        return -1;
      debug ("client sent %s to abort the connection",
             name_of_nbd_opt (option));
      return -1;

    case NBD_OPT_LIST:
      if (optlen != 0) {
        if (send_newstyle_option_reply (option, NBD_REP_ERR_INVALID)
            == -1)
          return -1;
        if (conn_recv_full (data, optlen,
                            "read: %s: %m", name_of_nbd_opt (option)) == -1)
          return -1;
        continue;
      }

      /* Send back the exportname list. */
      debug ("newstyle negotiation: %s: advertising exports",
             name_of_nbd_opt (option));
      if (send_newstyle_option_reply_exportnames (option) == -1)
        return -1;
      break;

    case NBD_OPT_STARTTLS:
      if (optlen != 0) {
        if (send_newstyle_option_reply (option, NBD_REP_ERR_INVALID)
            == -1)
          return -1;
        if (conn_recv_full (data, optlen,
                            "read: %s: %m", name_of_nbd_opt (option)) == -1)
          return -1;
        continue;
      }

      if (tls == 0) {           /* --tls=off (NOTLS mode). */
#ifdef HAVE_GNUTLS
#define NO_TLS_REPLY NBD_REP_ERR_POLICY
#else
#define NO_TLS_REPLY NBD_REP_ERR_UNSUP
#endif
        if (send_newstyle_option_reply (option, NO_TLS_REPLY) == -1)
          return -1;
      }
      else /* --tls=on or --tls=require */ {
        /* We can't upgrade to TLS twice on the same connection. */
        if (conn->using_tls) {
          if (send_newstyle_option_reply (option, NBD_REP_ERR_INVALID) == -1)
            return -1;
          continue;
        }

        /* We have to send the (unencrypted) reply before starting
         * the handshake.
         */
        if (send_newstyle_option_reply (option, NBD_REP_ACK) == -1)
          return -1;

        /* Upgrade the connection to TLS.  Also performs access control. */
        if (crypto_negotiate_tls (conn->sockin, conn->sockout) == -1)
          return -1;
        conn->using_tls = true;
        debug ("using TLS on this connection");
        /* Wipe out any cached state. */
        conn->structured_replies = false;
        free (conn->exportname_from_set_meta_context);
        conn->exportname_from_set_meta_context = NULL;
        conn->meta_context_base_allocation = false;
      }
      break;

    case NBD_OPT_INFO:
    case NBD_OPT_GO:
      if (conn_recv_full (data, optlen, "read: %s: %m", optname) == -1)
        return -1;

      if (optlen < 6) { /* 32 bit export length + 16 bit nr info */
        debug ("newstyle negotiation: %s option length < 6", optname);

        if (send_newstyle_option_reply (option, NBD_REP_ERR_INVALID)
            == -1)
          return -1;
        continue;
      }

      {
        uint32_t exportnamelen;
        uint16_t nrinfos;
        uint16_t info;
        size_t i;

        /* Validate the name length and number of INFO requests. */
        memcpy (&exportnamelen, &data[0], 4);
        exportnamelen = be32toh (exportnamelen);
        if (exportnamelen > optlen-6 /* NB optlen >= 6, see above */) {
          debug ("newstyle negotiation: %s: export name too long", optname);
          if (send_newstyle_option_reply (option, NBD_REP_ERR_INVALID)
              == -1)
            return -1;
          continue;
        }
        memcpy (&nrinfos, &data[exportnamelen+4], 2);
        nrinfos = be16toh (nrinfos);
        if (optlen != 4 + exportnamelen + 2 + 2*nrinfos) {
          debug ("newstyle negotiation: %s: "
                 "number of information requests incorrect", optname);
          if (send_newstyle_option_reply (option, NBD_REP_ERR_INVALID)
              == -1)
            return -1;
          continue;
        }

        /* As with NBD_OPT_EXPORT_NAME we print the export name and
         * save it in the connection.  If an earlier
         * NBD_OPT_SET_META_CONTEXT used an export name, it must match
         * or else we drop the support for that context.
         */
        if (check_export_name (option, &data[4], exportnamelen,
                               optlen - 6) == -1) {
          if (send_newstyle_option_reply (option, NBD_REP_ERR_INVALID)
              == -1)
            return -1;
          continue;
        }

        /* The spec is confusing, but it is required that we send back
         * NBD_INFO_EXPORT, even if the client did not request it!
         * qemu client in particular does not request this, but will
         * fail if we don't send it.  Note that if .open fails, but we
         * succeed at .close, then we merely return an error to the
         * client and let them try another NBD_OPT, rather than
         * disconnecting.
         */
        if (finish_newstyle_options (&exportsize,
                                     &data[4], exportnamelen) == -1) {
          if (backend_finalize (top) == -1)
            return -1;
          backend_close (top);
          if (send_newstyle_option_reply (option, NBD_REP_ERR_UNKNOWN) == -1)
            return -1;
          continue;
        }

        if (send_newstyle_option_reply_info_export (option,
                                                    NBD_REP_INFO,
                                                    NBD_INFO_EXPORT,
                                                    exportsize) == -1)
          return -1;

        /* For now we ignore all other info requests (but we must
         * ignore NBD_INFO_EXPORT if it was requested, because we
         * replied already above).  Therefore this loop doesn't do
         * much at the moment.
         */
        for (i = 0; i < nrinfos; ++i) {
          memcpy (&info, &data[4 + exportnamelen + 2 + i*2], 2);
          info = be16toh (info);
          switch (info) {
          case NBD_INFO_EXPORT: /* ignore - reply sent above */ break;
          default:
            debug ("newstyle negotiation: %s: "
                   "ignoring NBD_INFO_* request %u (%s)",
                   optname, (unsigned) info, name_of_nbd_info (info));
            break;
          }
        }
      }

      /* Unlike NBD_OPT_EXPORT_NAME, NBD_OPT_GO sends back an ACK
       * or ERROR packet.  If this was NBD_OPT_LIST, call .close.
       */
      if (send_newstyle_option_reply (option, NBD_REP_ACK) == -1)
        return -1;

      if (option == NBD_OPT_INFO) {
        if (backend_finalize (top) == -1)
          return -1;
        backend_close (top);
      }

      break;

    case NBD_OPT_STRUCTURED_REPLY:
      if (optlen != 0) {
        if (send_newstyle_option_reply (option, NBD_REP_ERR_INVALID)
            == -1)
          return -1;
        if (conn_recv_full (data, optlen,
                            "read: %s: %m", name_of_nbd_opt (option)) == -1)
          return -1;
        continue;
      }

      debug ("newstyle negotiation: %s: client requested structured replies",
             name_of_nbd_opt (option));

      if (no_sr) {
        /* Must fail with ERR_UNSUP for qemu 4.2 to remain happy;
         * but failing with ERR_POLICY would have been nicer.
         */
        if (send_newstyle_option_reply (option, NBD_REP_ERR_UNSUP) == -1)
          return -1;
        debug ("newstyle negotiation: %s: structured replies are disabled",
               name_of_nbd_opt (option));
        break;
      }

      if (send_newstyle_option_reply (option, NBD_REP_ACK) == -1)
        return -1;

      conn->structured_replies = true;
      break;

    case NBD_OPT_LIST_META_CONTEXT:
    case NBD_OPT_SET_META_CONTEXT:
      {
        uint32_t opt_index;
        uint32_t exportnamelen;
        uint32_t nr_queries;
        uint32_t querylen;
        const char *what;

        if (conn_recv_full (data, optlen, "read: %s: %m", optname) == -1)
          return -1;

        /* Note that we support base:allocation whether or not the plugin
         * supports can_extents.
         */
        if (!conn->structured_replies) {
          if (send_newstyle_option_reply (option, NBD_REP_ERR_INVALID)
              == -1)
            return -1;
          continue;
        }

        /* Minimum length of the option payload is:
         *   32 bit export name length followed by empty export name
         * + 32 bit number of queries followed by no queries
         * = 8 bytes.
         */
        what = "optlen < 8";
        if (optlen < 8) {
        opt_meta_invalid_option_len:
          debug ("newstyle negotiation: %s: invalid option length: %s",
                 optname, what);

          if (send_newstyle_option_reply (option, NBD_REP_ERR_INVALID)
              == -1)
            return -1;
          continue;
        }

        memcpy (&exportnamelen, &data[0], 4);
        exportnamelen = be32toh (exportnamelen);
        what = "validating export name";
        if (check_export_name (option, &data[4], exportnamelen,
                               optlen - 8) == -1)
          goto opt_meta_invalid_option_len;

        /* Remember the export name: the NBD spec says that if the client
         * later uses NBD_OPT_GO on a different export, then the context
         * returned here is not usable.
         */
        if (option == NBD_OPT_SET_META_CONTEXT) {
          conn->exportname_from_set_meta_context =
            strndup (&data[4], exportnamelen);
          if (conn->exportname_from_set_meta_context == NULL) {
            nbdkit_error ("malloc: %m");
            return -1;
          }
        }

        opt_index = 4 + exportnamelen;

        /* Read the number of queries. */
        what = "reading number of queries";
        if (opt_index+4 > optlen)
          goto opt_meta_invalid_option_len;
        memcpy (&nr_queries, &data[opt_index], 4);
        nr_queries = be32toh (nr_queries);
        opt_index += 4;

        /* for LIST: nr_queries == 0 means return all meta contexts
         * for SET: nr_queries == 0 means reset all contexts
         */
        debug ("newstyle negotiation: %s: %s count: %d", optname,
               option == NBD_OPT_LIST_META_CONTEXT ? "query" : "set",
               nr_queries);
        if (option == NBD_OPT_SET_META_CONTEXT)
          conn->meta_context_base_allocation = false;
        if (nr_queries == 0) {
          if (option == NBD_OPT_LIST_META_CONTEXT) {
            if (send_newstyle_option_reply_meta_context (option,
                                                         NBD_REP_META_CONTEXT,
                                                         0, "base:allocation")
                == -1)
              return -1;
          }

          if (send_newstyle_option_reply (option, NBD_REP_ACK) == -1)
            return -1;
        }
        else {
          /* Read and answer each query. */
          while (nr_queries > 0) {
            what = "reading query string length";
            if (opt_index+4 > optlen)
              goto opt_meta_invalid_option_len;
            memcpy (&querylen, &data[opt_index], 4);
            querylen = be32toh (querylen);
            opt_index += 4;
            what = "reading query string";
            if (check_string (option, &data[opt_index], querylen,
                              optlen - opt_index, "meta context query") == -1)
              goto opt_meta_invalid_option_len;

            debug ("newstyle negotiation: %s: %s %.*s",
                   optname,
                   option == NBD_OPT_LIST_META_CONTEXT ? "query" : "set",
                   (int) querylen, &data[opt_index]);

            /* For LIST, "base:" returns all supported contexts in the
             * base namespace.  We only support "base:allocation".
             */
            if (option == NBD_OPT_LIST_META_CONTEXT &&
                querylen == 5 &&
                strncmp (&data[opt_index], "base:", 5) == 0) {
              if (send_newstyle_option_reply_meta_context
                  (option, NBD_REP_META_CONTEXT,
                   0, "base:allocation") == -1)
                return -1;
            }
            /* "base:allocation" requested by name. */
            else if (querylen == 15 &&
                     strncmp (&data[opt_index], "base:allocation", 15) == 0) {
              if (send_newstyle_option_reply_meta_context
                  (option, NBD_REP_META_CONTEXT,
                   option == NBD_OPT_SET_META_CONTEXT
                   ? base_allocation_id : 0,
                   "base:allocation") == -1)
                return -1;
              if (option == NBD_OPT_SET_META_CONTEXT)
                conn->meta_context_base_allocation = true;
            }
            /* Every other query must be ignored. */

            opt_index += querylen;
            nr_queries--;
          }
          if (send_newstyle_option_reply (option, NBD_REP_ACK) == -1)
            return -1;
        }
        debug ("newstyle negotiation: %s: reply complete", optname);
      }
      break;

    default:
      /* Unknown option. */
      if (send_newstyle_option_reply (option, NBD_REP_ERR_UNSUP) == -1)
        return -1;
      if (conn_recv_full (data, optlen,
                          "reading unknown option data: conn->recv: %m") == -1)
        return -1;
    }

    /* Note, since it's not very clear from the protocol doc, that the
     * client must send NBD_OPT_EXPORT_NAME or NBD_OPT_GO last, and
     * that ends option negotiation.
     */
    if (option == NBD_OPT_EXPORT_NAME || option == NBD_OPT_GO)
      break;
  }

  if (nr_options >= MAX_NR_OPTIONS) {
    nbdkit_error ("client exceeded maximum number of options (%d)",
                  MAX_NR_OPTIONS);
    return -1;
  }

  /* In --tls=require / FORCEDTLS mode, we must have upgraded to TLS
   * by the time we finish option negotiation.  If not, give up.
   */
  if (tls == 2 && !conn->using_tls) {
    nbdkit_error ("non-TLS client tried to connect in --tls=require mode");
    return -1;
  }

  return 0;
}

int
protocol_handshake_newstyle (void)
{
  GET_CONN;
  struct nbd_new_handshake handshake;
  uint16_t gflags;

  gflags = (NBD_FLAG_FIXED_NEWSTYLE | NBD_FLAG_NO_ZEROES) & mask_handshake;

  debug ("newstyle negotiation: flags: global 0x%x", gflags);

  handshake.nbdmagic = htobe64 (NBD_MAGIC);
  handshake.version = htobe64 (NBD_NEW_VERSION);
  handshake.gflags = htobe16 (gflags);

  if (conn->send (&handshake, sizeof handshake, 0) == -1) {
    nbdkit_error ("write: %s: %m", "sending newstyle handshake");
    return -1;
  }

  /* Client now sends us its 32 bit flags word ... */
  if (conn_recv_full (&conn->cflags, sizeof conn->cflags,
                      "reading initial client flags: conn->recv: %m") == -1)
    return -1;
  conn->cflags = be32toh (conn->cflags);
  /* ... which we check for accuracy. */
  debug ("newstyle negotiation: client flags: 0x%x", conn->cflags);
  if (conn->cflags & ~gflags) {
    nbdkit_error ("client requested unexpected flags 0x%x", conn->cflags);
    return -1;
  }

  /* Receive newstyle options. */
  if (negotiate_handshake_newstyle_options () == -1)
    return -1;

  return 0;
}
