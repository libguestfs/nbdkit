/* nbdkit
 * Copyright (C) 2013-2017 Red Hat Inc.
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

#include <nbdkit-plugin.h>

#include <vixDiskLib.h>

#define VDDK_MAJOR 5
#define VDDK_MINOR 1

static char *config = NULL;                /* config */
static const char *cookie = NULL;          /* cookie */
static const char *filename = NULL;        /* file */
static const char *libdir = VDDK_LIBDIR;   /* libdir */
static int nfc_host_port = 0;              /* nfchostport */
static char *password = NULL;              /* password */
static int port = 0;                       /* port */
static const char *server_name = NULL;     /* server */
static const char *snapshot_moref = NULL;  /* snapshot */
static const char *thumb_print = NULL;     /* thumbprint */
static const char *transport_modes = NULL; /* transports */
static const char *username = NULL;        /* user */
static const char *vim_api_ver = NULL;     /* vimapiver */
static const char *vmx_spec = NULL;        /* vm */
static int is_remote = 0;

#define VDDK_ERROR(err, fs, ...)                                \
  do {                                                          \
    char *vddk_err_msg;                                         \
    vddk_err_msg = VixDiskLib_GetErrorText ((err), NULL);       \
    nbdkit_error (fs ": %s", ##__VA_ARGS__, vddk_err_msg);      \
    VixDiskLib_FreeErrorText (vddk_err_msg);                    \
  } while (0)

#define DEBUG_CALL(fn, fs, ...)                                 \
  nbdkit_debug ("VDDK call: %s (" fs ")", fn, ##__VA_ARGS__)

static void
trim (char *str)
{
  size_t len = strlen (str);

  if (len > 0 && str[len-1] == '\n')
    str[len-1] = '\0';
}

/* Turn log messages from the library into nbdkit_debug. */
static void
debug_function (const char *fs, va_list args)
{
  char *str;

  if (vasprintf (&str, fs, args) == -1) {
    nbdkit_debug ("lost debug message: %s", fs);
    return;
  }

  trim (str);

  nbdkit_debug ("%s", str);
  free (str);
}

/* Turn error messages from the library into nbdkit_error. */
static void
error_function (const char *fs, va_list args)
{
  char *str;

  if (vasprintf (&str, fs, args) == -1) {
    nbdkit_error ("lost error message: %s", fs);
    return;
  }

  trim (str);

  nbdkit_error ("%s", str);
  free (str);
}

/* Load and unload the plugin. */
static void
vddk_load (void)
{
  VixError err;

  DEBUG_CALL ("VixDiskLib_InitEx",
              "%d, %d, &debug_fn, &error_fn, &error_fn, %s, %s",
              VDDK_MAJOR, VDDK_MINOR, libdir, config ? : "NULL");
  err = VixDiskLib_InitEx (VDDK_MAJOR, VDDK_MINOR,
                           &debug_function, /* log function */
                           &error_function, /* warn function */
                           &error_function, /* panic function */
                           libdir, config);
  if (err != VIX_OK) {
    VDDK_ERROR (err, "VixDiskLib_InitEx");
    exit (EXIT_FAILURE);
  }
}

static void
vddk_unload (void)
{
  DEBUG_CALL ("VixDiskLib_Exit", "");
  VixDiskLib_Exit ();
  free (config);
  free (password);
}

/* Configuration. */
static int
vddk_config (const char *key, const char *value)
{
  if (strcmp (key, "config") == 0) {
    /* See FILENAMES AND PATHS in nbdkit-plugin(3). */
    free (config);
    config = nbdkit_realpath (value);
    if (!config)
      return -1;
  }
  else if (strcmp (key, "cookie") == 0) {
    cookie = value;
  }
  else if (strcmp (key, "file") == 0) {
    /* NB: Don't convert this to an absolute path, because in the
     * remote case this can be a path located on the VMware server.
     * For local paths the user must supply an absolute path.
     */
    filename = value;
  }
  else if (strcmp (key, "libdir") == 0) {
    libdir = value;
  }
  else if (strcmp (key, "nfchostport") == 0) {
#if HAVE_VIXDISKLIBCONNECTPARAMS_NFCHOSTPORT
    if (sscanf (value, "%d", &nfc_host_port) != 1) {
      nbdkit_error ("cannot parse nfchostport: %s", value);
      return -1;
    }
#else
    nbdkit_error ("this version of VDDK is too old to support nfchostpost");
    return -1;
#endif
  }
  else if (strcmp (key, "password") == 0) {
    free (password);
    if (nbdkit_read_password (value, &password) == -1)
      return -1;
  }
  else if (strcmp (key, "port") == 0) {
    if (sscanf (value, "%d", &port) != 1) {
      nbdkit_error ("cannot parse port: %s", value);
      return -1;
    }
  }
  else if (strcmp (key, "server") == 0) {
    server_name = value;
  }
  else if (strcmp (key, "snapshot") == 0) {
    snapshot_moref = value;
  }
  else if (strcmp (key, "thumbprint") == 0) {
    thumb_print = value;
  }
  else if (strcmp (key, "transports") == 0) {
    transport_modes = value;
  }
  else if (strcmp (key, "user") == 0) {
    username = value;
  }
  else if (strcmp (key, "vimapiver") == 0) {
#if HAVE_VIXDISKLIBCONNECTPARAMS_VIMAPIVER
    vim_api_ver = value;
#else
    nbdkit_error ("this version of VDDK is too old to support vimapiver");
    return -1;
#endif
  }
  else if (strcmp (key, "vm") == 0) {
    vmx_spec = value;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int
vddk_config_complete (void)
{
  if (filename == NULL) {
    nbdkit_error ("you must supply the file=<FILENAME> parameter after the plugin name on the command line");
    return -1;
  }

  /* For remote connections, check all the parameters have been
   * passed.  Note that VDDK will segfault if parameters that it
   * expects are NULL (and there's no real way to tell what parameters
   * it is expecting).  This implements the same test that the VDDK
   * sample program does.
   */
  is_remote =
    vmx_spec ||
    server_name ||
    username ||
    password ||
    cookie ||
    thumb_print ||
    port ||
    nfc_host_port ||
    vim_api_ver;

  if (is_remote) {
#define missing(test, param)                                            \
    if (test) {                                                         \
      nbdkit_error ("remote connection requested, missing parameter: %s", \
                    param);                                             \
      return -1;                                                        \
    }
    missing (!server_name, "server");
    missing (!username, "user");
    missing (!password, "password");
    missing (!vmx_spec, "vm");
#undef missing
  }

  return 0;
}

#define vddk_config_help \
  "file=<FILENAME>     (required) The filename (eg. VMDK file) to serve.\n" \
  "Many optional parameters are supported, see nbdkit-vddk-plugin(3)."

static void
vddk_dump_plugin (void)
{
  printf ("vddk_default_libdir=%s\n", VDDK_LIBDIR);

#if HAVE_VIXDISKLIBCONNECTPARAMS_NFCHOSTPORT
  printf ("vddk_has_nfchostport=1\n");
#endif

#if HAVE_VIXDISKLIBCONNECTPARAMS_VIMAPIVER
  printf ("vddk_has_vimapiver=1\n");
#endif

  /* XXX We really need to print the version of the dynamically
   * linked library here, but VDDK does not provide it.
   */
}

/* XXX To really do threading correctly in accordance with the VDDK
 * documentation, we must do all open/close calls from a single
 * thread.  This is a huge pain.
 */
#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

/* The per-connection handle. */
struct vddk_handle {
  VixDiskLibConnection connection; /* connection */
  VixDiskLibHandle handle;         /* disk handle */
};

/* Create the per-connection handle. */
static void *
vddk_open (int readonly)
{
  struct vddk_handle *h;
  VixError err;
  uint32_t flags;
  VixDiskLibConnectParams params;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  memset (&params, 0, sizeof params);
  if (is_remote) {
    params.vmxSpec = (char *) vmx_spec;
    params.serverName = (char *) server_name;
    if (cookie == NULL) {
      params.credType = VIXDISKLIB_CRED_UID;
      params.creds.uid.userName = (char *) username;
      params.creds.uid.password = password;
    }
    else {
      params.credType = VIXDISKLIB_CRED_SESSIONID;
      params.creds.sessionId.cookie = (char *) cookie;
      params.creds.sessionId.userName = (char *) username;
      params.creds.sessionId.key = password;
    }
    params.thumbPrint = (char *) thumb_print;
    params.port = port;
#if HAVE_VIXDISKLIBCONNECTPARAMS_NFCHOSTPORT
    params.nfcHostPort = nfc_host_port;
#endif
#if HAVE_VIXDISKLIBCONNECTPARAMS_VIMAPIVER
    params.vimApiVer = (char *) vim_api_ver;
#endif
  }

  /* XXX Some documentation suggests we should call
   * VixDiskLib_PrepareForAccess here.  However we need the true VM
   * name to do that.
   */

  DEBUG_CALL ("VixDiskLib_ConnectEx",
              "&params, %d, %s, %s, &connection",
              readonly,
              snapshot_moref ? : "NULL",
              transport_modes ? : "NULL");
  err = VixDiskLib_ConnectEx (&params,
                              readonly,
                              snapshot_moref,
                              transport_modes,
                              &h->connection);
  if (err != VIX_OK) {
    VDDK_ERROR (err, "VixDiskLib_ConnectEx");
    goto err1;
  }

  flags = 0;
  if (readonly)
    flags |= VIXDISKLIB_FLAG_OPEN_READ_ONLY;

  DEBUG_CALL ("VixDiskLib_Open",
              "connection, %s, %d, &handle", filename, flags);
  err = VixDiskLib_Open (h->connection, filename, flags, &h->handle);
  if (err != VIX_OK) {
    VDDK_ERROR (err, "VixDiskLib_Open: %s", filename);
    goto err2;
  }

  nbdkit_debug ("transport mode: %s",
                VixDiskLib_GetTransportMode (h->handle));

  return h;

 err2:
  DEBUG_CALL ("VixDiskLib_Disconnect", "connection");
  VixDiskLib_Disconnect (h->connection);
 err1:
  free (h);
  return NULL;
}

/* Free up the per-connection handle. */
static void
vddk_close (void *handle)
{
  struct vddk_handle *h = handle;

  DEBUG_CALL ("VixDiskLib_Close", "handle");
  VixDiskLib_Close (h->handle);
  DEBUG_CALL ("VixDiskLib_Disconnect", "connection");
  VixDiskLib_Disconnect (h->connection);
  free (h);
}

/* Get the file size. */
static int64_t
vddk_get_size (void *handle)
{
  struct vddk_handle *h = handle;
  VixDiskLibInfo *info;
  VixError err;
  uint64_t size;

  DEBUG_CALL ("VixDiskLib_GetInfo", "handle, &info");
  err = VixDiskLib_GetInfo (h->handle, &info);
  if (err != VIX_OK) {
    VDDK_ERROR (err, "VixDiskLib_GetInfo");
    return -1;
  }

  size = info->capacity * (uint64_t)VIXDISKLIB_SECTOR_SIZE;

  DEBUG_CALL ("VixDiskLib_FreeInfo", "info");
  VixDiskLib_FreeInfo (info);

  return (int64_t) size;
}

/* Read data from the file.
 *
 * Note that reads have to be aligned to sectors (XXX).
 */
static int
vddk_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  struct vddk_handle *h = handle;
  VixError err;

  /* Align to sectors. */
  if ((offset & (VIXDISKLIB_SECTOR_SIZE-1)) != 0) {
    nbdkit_error ("read is not aligned to sectors");
    return -1;
  }
  if ((count & (VIXDISKLIB_SECTOR_SIZE-1)) != 0) {
    nbdkit_error ("read is not aligned to sectors");
    return -1;
  }
  offset /= VIXDISKLIB_SECTOR_SIZE;
  count /= VIXDISKLIB_SECTOR_SIZE;

  DEBUG_CALL ("VixDiskLib_Read",
              "handle, %" PRIu64 ", %" PRIu32 ", buffer", offset, count);
  err = VixDiskLib_Read (h->handle, offset, count, buf);
  if (err != VIX_OK) {
    VDDK_ERROR (err, "VixDiskLib_Read");
    return -1;
  }

  return 0;
}

/* Write data to the file.
 *
 * Note that writes have to be aligned to sectors (XXX).
 */
static int
vddk_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset)
{
  struct vddk_handle *h = handle;
  VixError err;

  /* Align to sectors. */
  if ((offset & (VIXDISKLIB_SECTOR_SIZE-1)) != 0) {
    nbdkit_error ("read is not aligned to sectors");
    return -1;
  }
  if ((count & (VIXDISKLIB_SECTOR_SIZE-1)) != 0) {
    nbdkit_error ("read is not aligned to sectors");
    return -1;
  }
  offset /= VIXDISKLIB_SECTOR_SIZE;
  count /= VIXDISKLIB_SECTOR_SIZE;

  DEBUG_CALL ("VixDiskLib_Write",
              "handle, %" PRIu64 ", %" PRIu32 ", buffer", offset, count);
  err = VixDiskLib_Write (h->handle, offset, count, buf);
  if (err != VIX_OK) {
    VDDK_ERROR (err, "VixDiskLib_Write");
    return -1;
  }

  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "vddk",
  .longname          = "VMware VDDK plugin",
  .version           = PACKAGE_VERSION,
  .load              = vddk_load,
  .unload            = vddk_unload,
  .config            = vddk_config,
  .config_complete   = vddk_config_complete,
  .config_help       = vddk_config_help,
  .dump_plugin       = vddk_dump_plugin,
  .open              = vddk_open,
  .close             = vddk_close,
  .get_size          = vddk_get_size,
  .pread             = vddk_pread,
  .pwrite            = vddk_pwrite,
};

NBDKIT_REGISTER_PLUGIN(plugin)
