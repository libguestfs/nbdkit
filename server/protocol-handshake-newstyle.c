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

/* Maximum number of client options we allow before giving up. */
#define MAX_NR_OPTIONS 32

/* Maximum length of any option data (bytes). */
#define MAX_OPTION_LENGTH 4096

/* Receive newstyle options. */
static int
send_newstyle_option_reply (struct connection *conn,
                            uint32_t option, uint32_t reply)
{
  struct fixed_new_option_reply fixed_new_option_reply;

  fixed_new_option_reply.magic = htobe64 (NBD_REP_MAGIC);
  fixed_new_option_reply.option = htobe32 (option);
  fixed_new_option_reply.reply = htobe32 (reply);
  fixed_new_option_reply.replylen = htobe32 (0);

  if (conn->send (conn,
                  &fixed_new_option_reply,
                  sizeof fixed_new_option_reply) == -1) {
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

static int
send_newstyle_option_reply_exportname (struct connection *conn,
                                       uint32_t option, uint32_t reply,
                                       const char *exportname)
{
  struct fixed_new_option_reply fixed_new_option_reply;
  size_t name_len = strlen (exportname);
  uint32_t len;

  fixed_new_option_reply.magic = htobe64 (NBD_REP_MAGIC);
  fixed_new_option_reply.option = htobe32 (option);
  fixed_new_option_reply.reply = htobe32 (reply);
  fixed_new_option_reply.replylen = htobe32 (name_len + sizeof (len));

  if (conn->send (conn,
                  &fixed_new_option_reply,
                  sizeof fixed_new_option_reply) == -1) {
    nbdkit_error ("write: %s: %m", name_of_nbd_opt (option));
    return -1;
  }

  len = htobe32 (name_len);
  if (conn->send (conn, &len, sizeof len) == -1) {
    nbdkit_error ("write: %s: %s: %m",
                  name_of_nbd_opt (option), "sending length");
    return -1;
  }
  if (conn->send (conn, exportname, name_len) == -1) {
    nbdkit_error ("write: %s: %s: %m",
                  name_of_nbd_opt (option), "sending export name");
    return -1;
  }

  return 0;
}

static int
send_newstyle_option_reply_info_export (struct connection *conn,
                                        uint32_t option, uint32_t reply,
                                        uint16_t info)
{
  struct fixed_new_option_reply fixed_new_option_reply;
  struct fixed_new_option_reply_info_export export;

  fixed_new_option_reply.magic = htobe64 (NBD_REP_MAGIC);
  fixed_new_option_reply.option = htobe32 (option);
  fixed_new_option_reply.reply = htobe32 (reply);
  fixed_new_option_reply.replylen = htobe32 (sizeof export);
  export.info = htobe16 (info);
  export.exportsize = htobe64 (conn->exportsize);
  export.eflags = htobe16 (conn->eflags);

  if (conn->send (conn,
                  &fixed_new_option_reply,
                  sizeof fixed_new_option_reply) == -1 ||
      conn->send (conn, &export, sizeof export) == -1) {
    nbdkit_error ("write: %s: %m", name_of_nbd_opt (option));
    return -1;
  }

  return 0;
}

static int
send_newstyle_option_reply_meta_context (struct connection *conn,
                                         uint32_t option, uint32_t reply,
                                         uint32_t context_id,
                                         const char *name)
{
  struct fixed_new_option_reply fixed_new_option_reply;
  struct fixed_new_option_reply_meta_context context;
  const size_t namelen = strlen (name);

  debug ("newstyle negotiation: %s: replying with %s id %d",
         name_of_nbd_opt (option), name, context_id);
  fixed_new_option_reply.magic = htobe64 (NBD_REP_MAGIC);
  fixed_new_option_reply.option = htobe32 (option);
  fixed_new_option_reply.reply = htobe32 (reply);
  fixed_new_option_reply.replylen = htobe32 (sizeof context + namelen);
  context.context_id = htobe32 (context_id);

  if (conn->send (conn,
                  &fixed_new_option_reply,
                  sizeof fixed_new_option_reply) == -1 ||
      conn->send (conn, &context, sizeof context) == -1 ||
      conn->send (conn, name, namelen) == -1) {
    nbdkit_error ("write: %s: %m", name_of_nbd_opt (option));
    return -1;
  }

  return 0;
}

/* Sub-function during negotiate_handshake_newstyle, to uniformly handle
 * a client hanging up on a message boundary.
 */
static int __attribute__ ((format (printf, 4, 5)))
conn_recv_full (struct connection *conn, void *buf, size_t len,
                const char *fmt, ...)
{
  int r = conn->recv (conn, buf, len);
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
 * in that function.
 */
static int
finish_newstyle_options (struct connection *conn)
{
  if (protocol_common_open (conn, &conn->exportsize, &conn->eflags) == -1)
    return -1;

  debug ("newstyle negotiation: flags: export 0x%x", conn->eflags);
  return 0;
}

static int
negotiate_handshake_newstyle_options (struct connection *conn)
{
  struct new_option new_option;
  size_t nr_options;
  uint64_t version;
  uint32_t option;
  uint32_t optlen;
  char data[MAX_OPTION_LENGTH+1];
  struct new_handshake_finish handshake_finish;
  const char *optname;

  for (nr_options = 0; nr_options < MAX_NR_OPTIONS; ++nr_options) {
    if (conn_recv_full (conn, &new_option, sizeof new_option,
                        "reading option: conn->recv: %m") == -1)
      return -1;

    version = be64toh (new_option.version);
    if (version != NEW_VERSION) {
      nbdkit_error ("unknown option version %" PRIx64
                    ", expecting %" PRIx64,
                    version, NEW_VERSION);
      return -1;
    }

    /* There is a maximum option length we will accept, regardless
     * of the option type.
     */
    optlen = be32toh (new_option.optlen);
    if (optlen > MAX_OPTION_LENGTH) {
      nbdkit_error ("client option data too long (%" PRIu32 ")", optlen);
      return -1;
    }

    option = be32toh (new_option.option);
    optname = name_of_nbd_opt (option);

    /* In --tls=require / FORCEDTLS mode the only options allowed
     * before TLS negotiation are NBD_OPT_ABORT and NBD_OPT_STARTTLS.
     */
    if (tls == 2 && !conn->using_tls &&
        !(option == NBD_OPT_ABORT || option == NBD_OPT_STARTTLS)) {
      if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_TLS_REQD))
        return -1;
      continue;
    }

    switch (option) {
    case NBD_OPT_EXPORT_NAME:
      if (conn_recv_full (conn, data, optlen,
                          "read: %s: %m", name_of_nbd_opt (option)) == -1)
        return -1;
      /* Apart from printing it, ignore the export name. */
      data[optlen] = '\0';
      debug ("newstyle negotiation: %s: "
             "client requested export '%s' (ignored)",
             name_of_nbd_opt (option), data);

      /* We have to finish the handshake by sending handshake_finish. */
      if (finish_newstyle_options (conn) == -1)
        return -1;

      memset (&handshake_finish, 0, sizeof handshake_finish);
      handshake_finish.exportsize = htobe64 (conn->exportsize);
      handshake_finish.eflags = htobe16 (conn->eflags);

      if (conn->send (conn,
                      &handshake_finish,
                      (conn->cflags & NBD_FLAG_NO_ZEROES)
                      ? offsetof (struct new_handshake_finish, zeroes)
                      : sizeof handshake_finish) == -1) {
        nbdkit_error ("write: %s: %m", optname);
        return -1;
      }
      break;

    case NBD_OPT_ABORT:
      if (send_newstyle_option_reply (conn, option, NBD_REP_ACK) == -1)
        return -1;
      debug ("client sent %s to abort the connection",
             name_of_nbd_opt (option));
      return -1;

    case NBD_OPT_LIST:
      if (optlen != 0) {
        if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_INVALID)
            == -1)
          return -1;
        if (conn_recv_full (conn, data, optlen,
                            "read: %s: %m", name_of_nbd_opt (option)) == -1)
          return -1;
        continue;
      }

      /* Send back the exportname. */
      debug ("newstyle negotiation: %s: advertising export '%s'",
             name_of_nbd_opt (option), exportname);
      if (send_newstyle_option_reply_exportname (conn, option, NBD_REP_SERVER,
                                                 exportname) == -1)
        return -1;

      if (send_newstyle_option_reply (conn, option, NBD_REP_ACK) == -1)
        return -1;
      break;

    case NBD_OPT_STARTTLS:
      if (optlen != 0) {
        if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_INVALID)
            == -1)
          return -1;
        if (conn_recv_full (conn, data, optlen,
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
        if (send_newstyle_option_reply (conn, option, NO_TLS_REPLY) == -1)
          return -1;
      }
      else /* --tls=on or --tls=require */ {
        /* We can't upgrade to TLS twice on the same connection. */
        if (conn->using_tls) {
          if (send_newstyle_option_reply (conn, option,
                                          NBD_REP_ERR_INVALID) == -1)
            return -1;
          continue;
        }

        /* We have to send the (unencrypted) reply before starting
         * the handshake.
         */
        if (send_newstyle_option_reply (conn, option, NBD_REP_ACK) == -1)
          return -1;

        /* Upgrade the connection to TLS.  Also performs access control. */
        if (crypto_negotiate_tls (conn, conn->sockin, conn->sockout) == -1)
          return -1;
        conn->using_tls = true;
        debug ("using TLS on this connection");
      }
      break;

    case NBD_OPT_INFO:
    case NBD_OPT_GO:
      if (conn_recv_full (conn, data, optlen,
                          "read: %s: %m", optname) == -1)
        return -1;

      if (optlen < 6) { /* 32 bit export length + 16 bit nr info */
        debug ("newstyle negotiation: %s option length < 6", optname);

        if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_INVALID)
            == -1)
          return -1;
        continue;
      }

      {
        uint32_t exportnamelen;
        uint16_t nrinfos;
        uint16_t info;
        size_t i;
        CLEANUP_FREE char *requested_exportname = NULL;

        /* Validate the name length and number of INFO requests. */
        memcpy (&exportnamelen, &data[0], 4);
        exportnamelen = be32toh (exportnamelen);
        if (exportnamelen > optlen-6 /* NB optlen >= 6, see above */) {
          debug ("newstyle negotiation: %s: export name too long", optname);
          if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_INVALID)
              == -1)
            return -1;
          continue;
        }
        memcpy (&nrinfos, &data[exportnamelen+4], 2);
        nrinfos = be16toh (nrinfos);
        if (optlen != 4 + exportnamelen + 2 + 2*nrinfos) {
          debug ("newstyle negotiation: %s: "
                 "number of information requests incorrect", optname);
          if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_INVALID)
              == -1)
            return -1;
          continue;
        }

        /* As with NBD_OPT_EXPORT_NAME we print the export name and then
         * ignore it.
         */
        requested_exportname = malloc (exportnamelen+1);
        if (requested_exportname == NULL) {
          nbdkit_error ("malloc: %m");
          return -1;
        }
        memcpy (requested_exportname, &data[4], exportnamelen);
        requested_exportname[exportnamelen] = '\0';
        debug ("newstyle negotiation: %s: "
               "client requested export '%s' (ignored)",
               optname, requested_exportname);

        /* The spec is confusing, but it is required that we send back
         * NBD_INFO_EXPORT, even if the client did not request it!
         * qemu client in particular does not request this, but will
         * fail if we don't send it.
         */
        if (finish_newstyle_options (conn) == -1)
          return -1;

        if (send_newstyle_option_reply_info_export (conn, option,
                                                    NBD_REP_INFO,
                                                    NBD_INFO_EXPORT) == -1)
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
       * or ERROR packet.
       */
      if (send_newstyle_option_reply (conn, option, NBD_REP_ACK) == -1)
        return -1;

      break;

    case NBD_OPT_STRUCTURED_REPLY:
      if (optlen != 0) {
        if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_INVALID)
            == -1)
          return -1;
        if (conn_recv_full (conn, data, optlen,
                            "read: %s: %m", name_of_nbd_opt (option)) == -1)
          return -1;
        continue;
      }

      debug ("newstyle negotiation: %s: client requested structured replies",
             name_of_nbd_opt (option));

      if (send_newstyle_option_reply (conn, option, NBD_REP_ACK) == -1)
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

        if (conn_recv_full (conn, data, optlen, "read: %s: %m", optname) == -1)
          return -1;

        if (!conn->structured_replies) {
          if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_INVALID)
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

          if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_INVALID)
              == -1)
            return -1;
          continue;
        }

        /* Discard the export name. */
        memcpy (&exportnamelen, &data[0], 4);
        exportnamelen = be32toh (exportnamelen);
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
        if (nr_queries == 0) {
          if (option == NBD_OPT_SET_META_CONTEXT)
            conn->meta_context_base_allocation = false;
          else /* LIST */ {
            if (send_newstyle_option_reply_meta_context
                (conn, option, NBD_REP_META_CONTEXT,
                 0, "base:allocation") == -1)
              return -1;
          }

          if (send_newstyle_option_reply (conn, option, NBD_REP_ACK) == -1)
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
            if (opt_index + querylen > optlen)
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
                  (conn, option, NBD_REP_META_CONTEXT,
                   0, "base:allocation") == -1)
                return -1;
            }
            /* "base:allocation" requested by name. */
            else if (querylen == 15 &&
                     strncmp (&data[opt_index], "base:allocation", 15) == 0) {
              if (send_newstyle_option_reply_meta_context
                  (conn, option, NBD_REP_META_CONTEXT,
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
          if (send_newstyle_option_reply (conn, option, NBD_REP_ACK) == -1)
            return -1;
        }
        debug ("newstyle negotiation: %s: reply complete", optname);
      }
      break;

    default:
      /* Unknown option. */
      if (send_newstyle_option_reply (conn, option, NBD_REP_ERR_UNSUP) == -1)
        return -1;
      if (conn_recv_full (conn, data, optlen,
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
protocol_handshake_newstyle (struct connection *conn)
{
  struct new_handshake handshake;
  uint16_t gflags;

  gflags = NBD_FLAG_FIXED_NEWSTYLE | NBD_FLAG_NO_ZEROES;

  debug ("newstyle negotiation: flags: global 0x%x", gflags);

  memcpy (handshake.nbdmagic, "NBDMAGIC", 8);
  handshake.version = htobe64 (NEW_VERSION);
  handshake.gflags = htobe16 (gflags);

  if (conn->send (conn, &handshake, sizeof handshake) == -1) {
    nbdkit_error ("write: %s: %m", "sending newstyle handshake");
    return -1;
  }

  /* Client now sends us its 32 bit flags word ... */
  if (conn_recv_full (conn, &conn->cflags, sizeof conn->cflags,
                      "reading initial client flags: conn->recv: %m") == -1)
    return -1;
  conn->cflags = be32toh (conn->cflags);
  /* ... which we check for accuracy. */
  debug ("newstyle negotiation: client flags: 0x%x", conn->cflags);
  if (conn->cflags & ~gflags) {
    nbdkit_error ("client requested unknown flags 0x%x", conn->cflags);
    return -1;
  }

  /* Receive newstyle options. */
  if (negotiate_handshake_newstyle_options (conn) == -1)
    return -1;

  return 0;
}
