/* nbdkit
 * Copyright (C) 2014-2019 Red Hat Inc.
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
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <libssh/libssh.h>
#include <libssh/sftp.h>

#include <nbdkit-plugin.h>

#include "minmax.h"

static const char *host = NULL;
static const char *path = NULL;
static const char *port = NULL;
static const char *user = NULL;
static char *password = NULL;

/* config can be:
 * NULL => parse options from default file
 * "" => do NOT parse options
 * some filename => parse options from filename
 */
static const char *config = NULL;

/* Use '-D ssh.log=N' to set.
 *
 * The log levels (N) are:
 *
 *   SSH_LOG_NONE  -> 0
 *   SSH_LOG_WARN  -> 1
 *   SSH_LOG_INFO  -> 2
 *   SSH_LOG_DEBUG -> 3
 *   SSH_LOG_TRACE -> 4
 */
int ssh_debug_log = 0;

static void
ssh_unload (void)
{
  free (password);
}

/* Called for each key=value passed on the command line. */
static int
ssh_config (const char *key, const char *value)
{
  if (strcmp (key, "host") == 0)
    host = value;

  else if (strcmp (key, "path") == 0)
    path = value;

  else if (strcmp (key, "port") == 0)
    port = value;

  else if (strcmp (key, "user") == 0)
    user = value;

  else if (strcmp (key, "password") == 0) {
    free (password);
    if (nbdkit_read_password (value, &password) == -1)
      return -1;
  }

  else if (strcmp (key, "config") == 0)
    config = value;

  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

/* The host and path parameters are mandatory. */
static int
ssh_config_complete (void)
{
  if (host == NULL || path == NULL) {
    nbdkit_error ("you must supply the host and path parameters "
                  "after the plugin name on the command line");
    return -1;
  }

  ssh_set_log_level (ssh_debug_log);

  return 0;
}

#define ssh_config_help \
  "host=<HOST>     (required) SSH server hostname.\n" \
  "path=<PATH>     (required) SSH remote path.\n" \
  "port=<PORT>                SSH protocol port number.\n" \
  "user=<USER>                SSH user name.\n" \
  "password=<PASSWORD>        SSH password.\n" \
  "config=<CONFIG>            Alternate local SSH configuration file." \

/* The per-connection handle. */
struct ssh_handle {
  ssh_session session;
  sftp_session sftp;
  sftp_file file;
};

/* Verify the remote host.
 * See: http://api.libssh.org/master/libssh_tutor_guided_tour.html
 */
static int
verify_remote_host (struct ssh_handle *h)
{
  enum ssh_known_hosts_e state;
  unsigned char *hash = NULL;
  ssh_key srv_pubkey = NULL;
  size_t hlen;
  int rc;

  rc = ssh_get_server_publickey (h->session, &srv_pubkey);
  if (rc < 0) {
    nbdkit_error ("could not get server public key");
    return -1;
  }
  rc = ssh_get_publickey_hash (srv_pubkey,
                               SSH_PUBLICKEY_HASH_SHA1,
                               &hash, &hlen);
  ssh_key_free(srv_pubkey);
  if (rc < 0) {
    nbdkit_error ("could not get server public key SHA1 hash");
    return -1;
  }

  state = ssh_session_is_known_server (h->session);
  switch (state) {
  case SSH_KNOWN_HOSTS_OK:
    /* OK */
    break;

  case SSH_KNOWN_HOSTS_CHANGED:
    nbdkit_error ("host key for server changed");
    ssh_clean_pubkey_hash (&hash);
    return -1;

  case SSH_KNOWN_HOSTS_OTHER:
    nbdkit_error ("host key for server was not found "
                  "but another type of key exists");
    ssh_clean_pubkey_hash (&hash);
    return -1;

  case SSH_KNOWN_HOSTS_NOT_FOUND:
    /* This is not actually an error, but the user must ensure the
     * host key is set up before using nbdkit so we error out here.
     */
    nbdkit_error ("could not find known_hosts file");
    ssh_clean_pubkey_hash (&hash);
    return -1;

  case SSH_KNOWN_HOSTS_UNKNOWN:
    nbdkit_error ("host key is unknown, you must use ssh first "
                  "and accept the host key");
    ssh_clean_pubkey_hash (&hash);
    return -1;

  case SSH_KNOWN_HOSTS_ERROR:
    nbdkit_error ("known hosts error: %s", ssh_get_error (h->session));
    ssh_clean_pubkey_hash (&hash);
    return -1;
  }

  ssh_clean_pubkey_hash (&hash);
  return 0;
}

/* Authenticate.
 * See: http://api.libssh.org/master/libssh_tutor_authentication.html
 */
static int
authenticate_pubkey (ssh_session session)
{
  int rc;

  rc = ssh_userauth_publickey_auto (session, NULL, NULL);
  if (rc == SSH_AUTH_ERROR)
    nbdkit_debug ("public key authentication failed: %s",
                  ssh_get_error (session));

  return rc;
}

static int
authenticate_password (ssh_session session, const char *password)
{
  int rc;

  rc = ssh_userauth_password (session, NULL, password);
  if (rc == SSH_AUTH_ERROR)
    nbdkit_debug ("password authentication failed: %s",
                  ssh_get_error (session));
  return rc;
}

static int
authenticate (struct ssh_handle *h)
{
  int method, rc;

  rc = ssh_userauth_none (h->session, NULL);
  if (rc == SSH_AUTH_SUCCESS)
    return 0;
  if (rc == SSH_AUTH_ERROR)
    return -1;

  method = ssh_userauth_list (h->session, NULL);

  if (method & SSH_AUTH_METHOD_PUBLICKEY) {
    rc = authenticate_pubkey (h->session);
    if (rc == SSH_AUTH_SUCCESS) return 0;
  }

  /* Example code tries keyboard-interactive here, but we cannot use
   * that method from a server.
   */

  if (password != NULL && (method & SSH_AUTH_METHOD_PASSWORD)) {
    rc = authenticate_password (h->session, password);
    if (rc == SSH_AUTH_SUCCESS) return 0;
  }

  nbdkit_error ("all possible authentication methods failed");
  return -1;
}

/* Create the per-connection handle. */
static void *
ssh_open (int readonly)
{
  struct ssh_handle *h;
  const int set = 1;
  int r;
  int access_type;

  h = calloc (1, sizeof *h);
  if (h == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }

  /* Set up the SSH session. */
  h->session = ssh_new ();
  if (!h->session) {
    nbdkit_error ("failed to initialize libssh session");
    goto err;
  }

  /* Disable Nagle's algorithm which is recommended by the libssh
   * developers to improve performance of sftp.  Ignore any error if
   * we fail to set this.
   */
  ssh_options_set (h->session, SSH_OPTIONS_NODELAY, &set);

  r = ssh_options_set (h->session, SSH_OPTIONS_HOST, host);
  if (r != SSH_OK) {
    nbdkit_error ("failed to set host in libssh session: %s: %s",
                  host, ssh_get_error (h->session));
    goto err;
  }
  if (port != NULL) {
    r = ssh_options_set (h->session, SSH_OPTIONS_PORT_STR, port);
    if (r != SSH_OK) {
      nbdkit_error ("failed to set port in libssh session: %s: %s",
                    port, ssh_get_error (h->session));
      goto err;
    }
  }
  if (user != NULL) {
    r = ssh_options_set (h->session, SSH_OPTIONS_USER, user);
    if (r != SSH_OK) {
      nbdkit_error ("failed to set user in libssh session: %s: %s",
                    user, ssh_get_error (h->session));
      goto err;
    }
  }

  /* Read ~/.ssh/config or alternative file. */
  if (config == NULL || strcmp (config, "") != 0) {
    r = ssh_options_parse_config (h->session, config);
    if (r != SSH_OK) {
      nbdkit_error ("failed to parse local SSH configuration: %s",
                    ssh_get_error (h->session));
      goto err;
    }
  }

  /* Connect. */
  r = ssh_connect (h->session);
  if (r != SSH_OK) {
    nbdkit_error ("failed to connect to remote host: %s: %s",
                  host, ssh_get_error (h->session));
    goto err;
  }

  /* Verify the remote host. */
  if (verify_remote_host (h) == -1)
    goto err;

  /* Authenticate. */
  if (authenticate (h) == -1)
    goto err;

  /* Open the SFTP connection and file. */
  h->sftp = sftp_new (h->session);
  if (!h->sftp) {
    nbdkit_error ("failed to allocate sftp session: %s",
                  ssh_get_error (h->session));
    goto err;
  }
  r = sftp_init (h->sftp);
  if (r != SSH_OK) {
    nbdkit_error ("failed to initialize sftp session: %s",
                  ssh_get_error (h->session));
    goto err;
  }
  access_type = readonly ? O_RDONLY : O_RDWR;
  h->file = sftp_open (h->sftp, path, access_type, S_IRWXU);
  if (!h->file) {
    nbdkit_error ("cannot open file for %s: %s",
                  readonly ? "reading" : "writing",
                  ssh_get_error (h->session));
    goto err;
  }

  nbdkit_debug ("opened libssh handle");

  return h;

 err:
  if (h->file)
    sftp_close (h->file);
  if (h->sftp)
    sftp_free (h->sftp);
  if (h->session) {
    ssh_disconnect (h->session);
    ssh_free (h->session);
  }
  free (h);
  return NULL;
}

/* Free up the per-connection handle. */
static void
ssh_close (void *handle)
{
  struct ssh_handle *h = handle;
  int r;

  r = sftp_close (h->file);
  if (r != SSH_OK)
    nbdkit_error ("cannot close file: %s", ssh_get_error (h->session));

  sftp_free (h->sftp);
  ssh_disconnect (h->session);
  ssh_free (h->session);
  free (h);
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_REQUESTS

/* Get the file size. */
static int64_t
ssh_get_size (void *handle)
{
  struct ssh_handle *h = handle;
  sftp_attributes attrs;
  int64_t r;

  attrs = sftp_fstat (h->file);
  r = attrs->size;
  sftp_attributes_free (attrs);

  return r;
}

/* Read data from the remote server. */
static int
ssh_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  struct ssh_handle *h = handle;
  int r;
  ssize_t rs;

  r = sftp_seek64 (h->file, offset);
  if (r != SSH_OK) {
    nbdkit_error ("seek64 failed: %s", ssh_get_error (h->session));
    return -1;
  }

  while (count > 0) {
    rs = sftp_read (h->file, buf, count);
    if (rs < 0) {
      nbdkit_error ("read failed: %s (%zd)", ssh_get_error (h->session), rs);
      return -1;
    }
    buf += rs;
    count -= rs;
  }

  return 0;
}

/* Write data to the remote server. */
static int
ssh_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset)
{
  struct ssh_handle *h = handle;
  int r;
  ssize_t rs;

  r = sftp_seek64 (h->file, offset);
  if (r != SSH_OK) {
    nbdkit_error ("seek64 failed: %s", ssh_get_error (h->session));
    return -1;
  }

  while (count > 0) {
    /* Openssh has a maximum packet size of 256K, so any write
     * requests larger than this will fail in a peculiar way.  (This
     * limit doesn't seem to include the SFTP protocol overhead).
     * Therefore if the count is larger than 128K, reduce the size of
     * the request.  I don't know whether 256K is a limit that applies
     * to all servers.
     */
    rs = sftp_write (h->file, buf, MIN (count, 128*1024));
    if (rs < 0) {
      nbdkit_error ("write failed: %s (%zd)", ssh_get_error (h->session), rs);
      return -1;
    }
    buf += rs;
    count -= rs;
  }

  return 0;
}

static int
ssh_can_flush (void *handle)
{
  struct ssh_handle *h = handle;

  /* I added this extension to openssh 6.5 (April 2013).  It may not
   * be available in other SSH servers.
   */
  return sftp_extension_supported (h->sftp, "fsync@openssh.com", "1");
}

static int
ssh_flush (void *handle)
{
  struct ssh_handle *h = handle;
  int r;

 again:
  r = sftp_fsync (h->file);
  if (r == SSH_AGAIN)
    goto again;
  else if (r != SSH_OK) {
    nbdkit_error ("fsync failed: %s", ssh_get_error (h->session));
    return -1;
  }

  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "ssh",
  .version           = PACKAGE_VERSION,
  .unload            = ssh_unload,
  .config            = ssh_config,
  .config_complete   = ssh_config_complete,
  .config_help       = ssh_config_help,
  .open              = ssh_open,
  .close             = ssh_close,
  .get_size          = ssh_get_size,
  .pread             = ssh_pread,
  .pwrite            = ssh_pwrite,
  .can_flush         = ssh_can_flush,
  .flush             = ssh_flush,
};

NBDKIT_REGISTER_PLUGIN(plugin)
