/* nbdkit
 * Copyright (C) 2017-2019 Red Hat Inc.
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

#if defined(HAVE_GNUTLS) && defined(HAVE_GNUTLS_BASE64_DECODE2)
#include <gnutls/gnutls.h>
#define HAVE_BASE64 1
#endif

#define NBDKIT_API_VERSION 2

#include <nbdkit-plugin.h>

/* The mode. */
enum mode {
  MODE_EXPORTNAME,
  MODE_BASE64EXPORTNAME,
};
static enum mode mode = MODE_EXPORTNAME;

static int
reflection_config (const char *key, const char *value)
{
  if (strcmp (key, "mode") == 0) {
    if (strcasecmp (value, "exportname") == 0 ||
        strcasecmp (value, "export-name") == 0) {
      mode = MODE_EXPORTNAME;
    }
    else if (strcasecmp (value, "base64exportname") == 0 ||
             strcasecmp (value, "base64-export-name") == 0) {
#ifdef HAVE_BASE64
      mode = MODE_BASE64EXPORTNAME;
#else
      nbdkit_error ("the plugin was compiled without base64 support");
      return -1;
#endif
    }
    else {
      nbdkit_error ("unknown mode: '%s'", value);
      return -1;
    }
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

#define reflection_config_help \
  "mode=exportname|base64exportname  Plugin mode."

/* Provide a way to detect if the base64 feature is supported. */
static void
reflection_dump_plugin (void)
{
#ifdef HAVE_BASE64
  printf ("reflection_base64=yes\n");
#endif
}

/* Per-connection handle. */
struct handle {
  void *data;                   /* Block device data. */
  size_t len;                   /* Length of data in bytes. */
};

static int
decode_base64 (const char *data, size_t len, struct handle *ret)
{
#ifdef HAVE_BASE64
  gnutls_datum_t in, out;
  int err;

  /* For unclear reasons gnutls_base64_decode2 won't handle an empty
   * string, even though base64("") == "".
   * https://tools.ietf.org/html/rfc4648#section-10
   * https://gitlab.com/gnutls/gnutls/issues/834
   * So we have to special-case it.
   */
  if (len == 0) {
    ret->data = NULL;
    ret->len = 0;
    return 0;
  }

  in.data = (unsigned char *) data;
  in.size = len;
  err = gnutls_base64_decode2 (&in, &out);
  if (err != GNUTLS_E_SUCCESS) {
    nbdkit_error ("base64: %s", gnutls_strerror (err));
    /* We don't have to free out.data.  I verified that it is freed on
     * the error path of gnutls_base64_decode2.
     */
    return -1;
  }

  ret->data = out.data;         /* caller frees, eventually */
  ret->len = out.size;
  return 0;
#else
  nbdkit_error ("the plugin was compiled without base64 support");
  return -1;
#endif
}

/* Create the per-connection handle.
 *
 * This is a rather unusual plugin because it has to parse data sent
 * by the client.  For security reasons, be careful about:
 *
 * - Returning more data than is sent by the client.
 *
 * - Inputs that result in unbounded output.
 *
 * - Inputs that could hang, crash or exploit the server.
 */
static void *
reflection_open (int readonly)
{
  const char *export_name;
  size_t export_name_len;
  struct handle *h;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  switch (mode) {
  case MODE_EXPORTNAME:
  case MODE_BASE64EXPORTNAME:
    export_name = nbdkit_export_name ();
    if (export_name == NULL) {
      free (h);
      return NULL;
    }
    export_name_len = strlen (export_name);

    if (mode == MODE_EXPORTNAME) {
      h->len = export_name_len;
      h->data = strdup (export_name);
      if (h->data == NULL) {
        nbdkit_error ("strdup: %m");
        free (h);
        return NULL;
      }
      return h;
    }
    else /* mode == MODE_BASE64EXPORTNAME */ {
      if (decode_base64 (export_name, export_name_len, h) == -1) {
        free (h);
        return NULL;
      }
      return h;
    }

  default:
    abort ();
  }
}

/* Close the per-connection handle. */
static void
reflection_close (void *handle)
{
  struct handle *h = handle;

  free (h->data);
  free (h);
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Get the disk size. */
static int64_t
reflection_get_size (void *handle)
{
  struct handle *h = handle;

  return (int64_t) h->len;
}

/* Read-only plugin so multi-conn is safe. */
static int
reflection_can_multi_conn (void *handle)
{
  return 1;
}

/* Cache. */
static int
reflection_can_cache (void *handle)
{
  /* Everything is already in memory, returning this without
   * implementing .cache lets nbdkit do the correct no-op.
   */
  return NBDKIT_CACHE_NATIVE;
}

/* Read data. */
static int
reflection_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
                  uint32_t flags)
{
  struct handle *h = handle;

  memcpy (buf, h->data + offset, count);
  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "reflection",
  .version           = PACKAGE_VERSION,
  .config            = reflection_config,
  .config_help       = reflection_config_help,
  .dump_plugin       = reflection_dump_plugin,
  .magic_config_key  = "mode",
  .open              = reflection_open,
  .close             = reflection_close,
  .get_size          = reflection_get_size,
  .can_multi_conn    = reflection_can_multi_conn,
  .can_cache         = reflection_can_cache,
  .pread             = reflection_pread,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
