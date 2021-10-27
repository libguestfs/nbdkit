/* nbdkit
 * Copyright (C) 2013-2021 Red Hat Inc.
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
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <dlfcn.h>
#include <libgen.h>
#include <sys/time.h>

#include <pthread.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "minmax.h"
#include "rounding.h"
#include "tvdiff.h"
#include "vector.h"

#include "vddk.h"

/* Debug flags. */
NBDKIT_DLL_PUBLIC int vddk_debug_diskinfo;
NBDKIT_DLL_PUBLIC int vddk_debug_extents;
NBDKIT_DLL_PUBLIC int vddk_debug_datapath = 1;
NBDKIT_DLL_PUBLIC int vddk_debug_stats;

/* For each VDDK API define a global variable.  These globals are
 * initialized when the plugin is loaded (by vddk_get_ready).
 */
#define STUB(fn,ret,args) ret (*fn) args
#define OPTIONAL_STUB(fn,ret,args) ret (*fn) args
#include "vddk-stubs.h"
#undef STUB
#undef OPTIONAL_STUB

/* Parameters passed to InitEx. */
#define VDDK_MAJOR 6
#define VDDK_MINOR 5

void *dl;                              /* dlopen handle */
bool init_called;                      /* was InitEx called */
__thread int error_suppression;        /* threadlocal error suppression */
int library_version;                   /* VDDK major: 6, 7, ... */
bool is_remote;                        /* true if remote connection */

enum compression_type compression;     /* compression */
char *config;                          /* config */
const char *cookie;                    /* cookie */
const char *filename;                  /* file */
char *libdir;                          /* libdir */
uint16_t nfc_host_port;                /* nfchostport */
char *password;                        /* password */
uint16_t port;                         /* port */
const char *server_name;               /* server */
bool single_link;                      /* single-link */
const char *snapshot_moref;            /* snapshot */
const char *thumb_print;               /* thumbprint */
const char *transport_modes;           /* transports */
bool unbuffered;                       /* unbuffered */
const char *username;                  /* user */
const char *vmx_spec;                  /* vm */

/* For each VDDK API define a variable to store the time taken (used
 * to implement -D vddk.stats=1).
 */
struct vddk_stat {
  const char *name;             /* function name */
  int64_t usecs;                /* total number of usecs consumed */
  uint64_t calls;               /* number of times called */
  uint64_t bytes;               /* bytes transferred, datapath calls only */
};
static pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;
static void display_stats (void);
#define STUB(fn,ret,args) \
  static struct vddk_stat stats_##fn = { .name = #fn }
#define OPTIONAL_STUB(fn,ret,args) \
  static struct vddk_stat stats_##fn = { .name = #fn }
#include "vddk-stubs.h"
#undef STUB
#undef OPTIONAL_STUB

/* Unload the plugin. */
static void
vddk_unload (void)
{
  if (init_called) {
    VDDK_CALL_START (VixDiskLib_Exit, "")
      VixDiskLib_Exit ();
    VDDK_CALL_END (VixDiskLib_Exit, 0);
  }
  if (dl)
    dlclose (dl);

  if (vddk_debug_stats)
    display_stats ();

  free (config);
  free (libdir);
  free (password);
}

DEFINE_VECTOR_TYPE(statlist, struct vddk_stat)

static int
stat_compare (const void *vp1, const void *vp2)
{
  const struct vddk_stat *st1 = vp1;
  const struct vddk_stat *st2 = vp2;

  /* Note: sorts in reverse order of time spent in each API call. */
  if (st1->usecs < st2->usecs) return 1;
  else if (st1->usecs > st2->usecs) return -1;
  else return 0;
}

static const char *
api_name_without_prefix (const char *name)
{
  return strncmp (name, "VixDiskLib_", 11) == 0 ? name + 11 : name;
}

static void
display_stats (void)
{
  statlist stats = empty_vector;
  size_t i;

#define STUB(fn,ret,args) statlist_append (&stats, stats_##fn)
#define OPTIONAL_STUB(fn,ret,args) statlist_append (&stats, stats_##fn)
#include "vddk-stubs.h"
#undef STUB
#undef OPTIONAL_STUB

  qsort (stats.ptr, stats.size, sizeof stats.ptr[0], stat_compare);

  nbdkit_debug ("VDDK function stats (-D vddk.stats=1):");
  nbdkit_debug ("%-24s  %15s %5s %15s",
                "VixDiskLib_...", "µs", "calls", "bytes");
  for (i = 0; i < stats.size; ++i) {
    if (stats.ptr[i].usecs) {
      if (stats.ptr[i].bytes > 0)
        nbdkit_debug ("  %-22s %15" PRIi64 " %5" PRIu64 " %15" PRIu64,
                      api_name_without_prefix (stats.ptr[i].name),
                      stats.ptr[i].usecs,
                      stats.ptr[i].calls,
                      stats.ptr[i].bytes);
      else
        nbdkit_debug ("  %-22s %15" PRIi64 " %5" PRIu64,
                      api_name_without_prefix (stats.ptr[i].name),
                      stats.ptr[i].usecs,
                      stats.ptr[i].calls);
    }
  }
  statlist_reset (&stats);
}

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
  CLEANUP_FREE char *str = NULL;

  if (vasprintf (&str, fs, args) == -1) {
    nbdkit_debug ("lost debug message: %s", fs);
    return;
  }

  trim (str);

  nbdkit_debug ("%s", str);
}

/* Turn error messages from the library into nbdkit_error. */
static void
error_function (const char *fs, va_list args)
{
  CLEANUP_FREE char *str = NULL;

  /* If the thread-local error_suppression flag is non-zero then we
   * will suppress error messages from VDDK in this thread.
   */
  if (error_suppression) return;

  if (vasprintf (&str, fs, args) == -1) {
    nbdkit_error ("lost error message: %s", fs);
    return;
  }

  trim (str);

  /* VDDK 7 added a useless error message about their "phone home"
   * system called CEIP which only panics users.  Demote it to a debug
   * statement.  https://bugzilla.redhat.com/show_bug.cgi?id=1834267
   */
  if (strstr (str, "Get CEIP status failed") != NULL) {
    nbdkit_debug ("%s", str);
    return;
  }

  nbdkit_error ("%s", str);
}

/* Configuration. */
static int
vddk_config (const char *key, const char *value)
{
  int r;

  if (strcmp (key, "compression") == 0) {
    if (strcmp (value, "zlib") == 0)
      compression = ZLIB;
    else if (strcmp (value, "fastlz") == 0)
      compression = FASTLZ;
    else if (strcmp (value, "skipz") == 0)
      compression = SKIPZ;
    else if (strcmp (value, "none") == 0)
      compression = NONE;
    else {
      nbdkit_error ("unknown compression type: %s", value);
      return -1;
    }
  }
  else if (strcmp (key, "config") == 0) {
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
    /* See FILENAMES AND PATHS in nbdkit-plugin(3). */
    free (libdir);
    libdir = nbdkit_realpath (value);
    if (!libdir)
      return -1;
  }
  else if (strcmp (key, "nfchostport") == 0) {
    if (nbdkit_parse_uint16_t ("nfchostport", value, &nfc_host_port) == -1)
      return -1;
  }
  else if (strcmp (key, "noreexec") == 0) {
    /* This undocumented option disables reexec.  The caller must set
     * LD_LIBRARY_PATH correctly as for older versions of the plugin.
     * This option is only for use when debugging reexec, eg. to see
     * if it causing a problem.
     */
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    noreexec = r;
  }
  else if (strcmp (key, "password") == 0) {
    free (password);
    if (nbdkit_read_password (value, &password) == -1)
      return -1;
  }
  else if (strcmp (key, "port") == 0) {
    if (nbdkit_parse_uint16_t ("port", value, &port) == -1)
      return -1;
  }
  else if (strcmp (key, "reexeced_") == 0) {
    /* Special name because it is only for internal use. */
    reexeced = (char *)value;
  }
  else if (strcmp (key, "server") == 0) {
    server_name = value;
  }
  else if (strcmp (key, "single-link") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    single_link = r;
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
  else if (strcmp (key, "unbuffered") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    unbuffered = r;
  }
  else if (strcmp (key, "user") == 0) {
    username = value;
  }
  else if (strcmp (key, "vimapiver") == 0) {
    /* Ignored for backwards compatibility. */
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

/* Load the VDDK library. */
static void
load_library (bool load_error_is_fatal)
{
  static struct {
    const char *soname;
    int library_version;
  } libs[] = {
    /* Prefer the newest library in case multiple exist.  Check two
     * possible directories: the usual VDDK installation puts .so
     * files in an arch-specific subdirectory of $libdir (our minimum
     * supported version is VDDK 6.5, which only supports x64-64); but
     * our testsuite is easier to write if we point libdir directly to
     * a stub .so.
     */
    { "lib64/libvixDiskLib.so.7", 7 },
    { "libvixDiskLib.so.7",       7 },
    { "lib64/libvixDiskLib.so.6", 6 },
    { "libvixDiskLib.so.6",       6 },
    { NULL }
  };
  size_t i;
  CLEANUP_FREE char *orig_error = NULL;

  if (!libdir) {
    libdir = strdup (VDDK_LIBDIR);
    if (!libdir) {
      nbdkit_error ("strdup: %m");
      exit (EXIT_FAILURE);
    }
  }

  for (i = 0; libs[i].soname != NULL; ++i) {
    CLEANUP_FREE char *path;

    /* Set the full path so that dlopen will preferentially load the
     * system libraries from the same directory.
     */
    if (asprintf (&path, "%s/%s", libdir, libs[i].soname) == -1) {
      nbdkit_error ("asprintf: %m");
      exit (EXIT_FAILURE);
    }

    dl = dlopen (path, RTLD_NOW);
    if (dl != NULL) {
      library_version = libs[i].library_version;
      /* Now that we found the library, ensure that LD_LIBRARY_PATH
       * includes its directory for all future loads.  This may modify
       * path in-place and/or re-exec nbdkit, but that's okay.
       */
      reexec_if_needed (dirname (path));
      break;
    }
    if (i == 0) {
      orig_error = dlerror ();
      if (orig_error)
        orig_error = strdup (orig_error);
    }
  }
  if (dl == NULL) {
    if (!load_error_is_fatal)
      return;
    nbdkit_error ("%s\n\n"
                  "If '%s' is located on a non-standard path you may need to\n"
                  "set libdir=/path/to/vmware-vix-disklib-distrib.\n\n"
                  "See nbdkit-vddk-plugin(1) man page section \"LIBRARY LOCATION\" for details.",
                  orig_error ? : "(unknown error)", libs[0].soname);
    exit (EXIT_FAILURE);
  }

  assert (library_version >= 6);

  /* Load symbols. */
#define STUB(fn,ret,args)                                         \
  do {                                                            \
    fn = dlsym (dl, #fn);                                         \
    if (fn == NULL) {                                             \
      nbdkit_error ("required VDDK symbol \"%s\" is missing: %s", \
                    #fn, dlerror ());                             \
      exit (EXIT_FAILURE);                                        \
    }                                                             \
  } while (0)
#define OPTIONAL_STUB(fn,ret,args) fn = dlsym (dl, #fn)
#include "vddk-stubs.h"
#undef STUB
#undef OPTIONAL_STUB

  /* Additionally, VDDK version must be >= 6.5.  This was the first
   * version which introduced VixDiskLib_Wait symbol so we can check
   * for that.
   */
  if (VixDiskLib_Wait == NULL) {
    nbdkit_error ("VDDK version must be >= 6.5. "
                  "See nbdkit-vddk-plugin(1) man page section \"SUPPORTED VERSIONS OF VDDK\".");
    exit (EXIT_FAILURE);
  }
}

static int
vddk_config_complete (void)
{
  if (filename == NULL) {
    nbdkit_error ("you must supply the file=<FILENAME> parameter "
                  "after the plugin name on the command line");
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
    nfc_host_port;

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

  /* Restore original LD_LIBRARY_PATH after reexec. */
  if (restore_ld_library_path () == -1)
    return -1;

  return 0;
}

#define vddk_config_help \
  "[file=]<FILENAME>   (required) The filename (eg. VMDK file) to serve.\n" \
  "Many optional parameters are supported, see nbdkit-vddk-plugin(1)."

static int
vddk_get_ready (void)
{
  load_library (true);
  return 0;
}

/* Defer VDDK initialization until after fork because it is known to
 * create background threads from VixDiskLib_InitEx.  Unfortunately
 * error reporting from this callback is difficult, but we have
 * already checked in .get_ready that the library is dlopenable.
 *
 * For various hangs and failures which were caused by background
 * threads and fork see:
 * https://bugzilla.redhat.com/show_bug.cgi?id=1846309#c9
 * https://www.redhat.com/archives/libguestfs/2019-April/msg00090.html
 */
static int
vddk_after_fork (void)
{
  VixError err;

  /* Initialize VDDK library. */
  VDDK_CALL_START (VixDiskLib_InitEx,
                   "%d, %d, &debug_fn, &error_fn, &error_fn, %s, %s",
                   VDDK_MAJOR, VDDK_MINOR,
                   libdir, config ? : "NULL")
    err = VixDiskLib_InitEx (VDDK_MAJOR, VDDK_MINOR,
                             &debug_function, /* log function */
                             &error_function, /* warn function */
                             &error_function, /* panic function */
                             libdir, config);
  VDDK_CALL_END (VixDiskLib_InitEx, 0);
  if (err != VIX_OK) {
    VDDK_ERROR (err, "VixDiskLib_InitEx");
    exit (EXIT_FAILURE);
  }
  init_called = true;

  return 0;
}

static void
vddk_dump_plugin (void)
{
  load_library (false);

  printf ("vddk_default_libdir=%s\n", VDDK_LIBDIR);
  printf ("vddk_has_nfchostport=1\n");

  /* Because load_library (false) we might not have loaded VDDK, in
   * which case we didn't set library_version.  Note this cannot
   * happen in the normal (non-debug-plugin) path because there we use
   * load_library (true).
   */
  if (library_version > 0)
    printf ("vddk_library_version=%d\n", library_version);

#if defined(HAVE_DLADDR)
  /* It would be nice to print the version of VDDK from the shared
   * library, but VDDK does not provide it.  Instead we can get the
   * path to the library using the glibc extension dladdr, and then
   * resolve symlinks using realpath.  The final pathname should
   * contain the version number.
   */
  Dl_info info;
  CLEANUP_FREE char *p = NULL;
  if (dl != NULL &&
      dladdr (VixDiskLib_InitEx, &info) != 0 &&
      info.dli_fname != NULL &&
      (p = nbdkit_realpath (info.dli_fname)) != NULL) {
    printf ("vddk_dll=%s\n", p);
  }
#endif

  /* Note we print all VDDK APIs found here, not just the optional
   * ones.  That is so if we update the baseline VDDK in future and
   * make optional into required APIs, the output doesn't change.
   */
#define STUB(fn,ret,args) if (fn != NULL) printf ("%s=1\n", #fn);
#define OPTIONAL_STUB(fn,ret,args) STUB(fn,ret,args)
#include "vddk-stubs.h"
#undef STUB
#undef OPTIONAL_STUB
}

/* The rules on threads and VDDK are here:
 * https://code.vmware.com/docs/11750/virtual-disk-development-kit-programming-guide/GUID-6BE903E8-DC70-46D9-98E4-E34A2002C2AD.html
 *
 * Before nbdkit 1.22 we used SERIALIZE_ALL_REQUESTS.  Since nbdkit
 * 1.22 we changed this to SERIALIZE_REQUESTS and added a mutex around
 * calls to VixDiskLib_Open and VixDiskLib_Close.  This is not quite
 * within the letter of the rules, but is within the spirit.
 */
#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_REQUESTS

/* Lock protecting open/close calls - see above. */
static pthread_mutex_t open_close_lock = PTHREAD_MUTEX_INITIALIZER;

/* The per-connection handle. */
struct vddk_handle {
  VixDiskLibConnectParams *params; /* connection parameters */
  VixDiskLibConnection connection; /* connection */
  VixDiskLibHandle handle;         /* disk handle */
};

static inline VixDiskLibConnectParams *
allocate_connect_params (void)
{
  VixDiskLibConnectParams *ret;

  if (VixDiskLib_AllocateConnectParams != NULL) {
    VDDK_CALL_START (VixDiskLib_AllocateConnectParams, "")
      ret = VixDiskLib_AllocateConnectParams ();
    VDDK_CALL_END (VixDiskLib_AllocateConnectParams, 0);
  }
  else
    ret = calloc (1, sizeof (VixDiskLibConnectParams));

  return ret;
}

static inline void
free_connect_params (VixDiskLibConnectParams *params)
{
  /* Only use FreeConnectParams if AllocateConnectParams was
   * originally called.  Otherwise use free.
   */
  if (VixDiskLib_AllocateConnectParams != NULL) {
    VDDK_CALL_START (VixDiskLib_FreeConnectParams, "params")
      VixDiskLib_FreeConnectParams (params);
    VDDK_CALL_END (VixDiskLib_FreeConnectParams, 0);
  }
  else
    free (params);
}

/* Create the per-connection handle. */
static void *
vddk_open (int readonly)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&open_close_lock);
  struct vddk_handle *h;
  VixError err;
  uint32_t flags;
  const char *transport_mode;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  h->params = allocate_connect_params ();
  if (h->params == NULL) {
    nbdkit_error ("allocate VixDiskLibConnectParams: %m");
    goto err0;
  }

  if (is_remote) {
    h->params->vmxSpec = (char *) vmx_spec;
    h->params->serverName = (char *) server_name;
    if (cookie == NULL) {
      h->params->credType = VIXDISKLIB_CRED_UID;
      h->params->creds.uid.userName = (char *) username;
      h->params->creds.uid.password = password;
    }
    else {
      h->params->credType = VIXDISKLIB_CRED_SESSIONID;
      h->params->creds.sessionId.cookie = (char *) cookie;
      h->params->creds.sessionId.userName = (char *) username;
      h->params->creds.sessionId.key = password;
    }
    h->params->thumbPrint = (char *) thumb_print;
    h->params->port = port;
    h->params->nfcHostPort = nfc_host_port;
    h->params->specType = VIXDISKLIB_SPEC_VMX;
  }

  /* XXX We should call VixDiskLib_PrepareForAccess here.  It disables
   * live storage migration (Storage VMotion) of the VM while we are
   * accessing it, and may be required for "Advanced Transport modes".
   */

  VDDK_CALL_START (VixDiskLib_ConnectEx,
                   "h->params, %d, %s, %s, &connection",
                   readonly,
                   snapshot_moref ? : "NULL",
                   transport_modes ? : "NULL")
    err = VixDiskLib_ConnectEx (h->params,
                                readonly,
                                snapshot_moref,
                                transport_modes,
                                &h->connection);
  VDDK_CALL_END (VixDiskLib_ConnectEx, 0);
  if (err != VIX_OK) {
    VDDK_ERROR (err, "VixDiskLib_ConnectEx");
    goto err1;
  }

  flags = 0;
  if (readonly)
    flags |= VIXDISKLIB_FLAG_OPEN_READ_ONLY;
  if (single_link)
    flags |= VIXDISKLIB_FLAG_OPEN_SINGLE_LINK;
  if (unbuffered)
    flags |= VIXDISKLIB_FLAG_OPEN_UNBUFFERED;
  switch (compression) {
  case ZLIB:   flags |= VIXDISKLIB_FLAG_OPEN_COMPRESSION_ZLIB;   break;
  case FASTLZ: flags |= VIXDISKLIB_FLAG_OPEN_COMPRESSION_FASTLZ; break;
  case SKIPZ:  flags |= VIXDISKLIB_FLAG_OPEN_COMPRESSION_SKIPZ;  break;
  case NONE:   break;
  }

  VDDK_CALL_START (VixDiskLib_Open,
                   "connection, %s, %d, &handle", filename, flags)
    err = VixDiskLib_Open (h->connection, filename, flags, &h->handle);
  VDDK_CALL_END (VixDiskLib_Open, 0);
  if (err != VIX_OK) {
    VDDK_ERROR (err, "VixDiskLib_Open: %s", filename);
    goto err2;
  }

  VDDK_CALL_START (VixDiskLib_GetTransportMode, "handle")
    transport_mode = VixDiskLib_GetTransportMode (h->handle);
  VDDK_CALL_END (VixDiskLib_GetTransportMode, 0);
  nbdkit_debug ("transport mode: %s", transport_mode);

  return h;

 err2:
  VDDK_CALL_START (VixDiskLib_Disconnect, "connection")
    VixDiskLib_Disconnect (h->connection);
  VDDK_CALL_END (VixDiskLib_Disconnect, 0);
 err1:
  free_connect_params (h->params);
 err0:
  free (h);
  return NULL;
}

/* Free up the per-connection handle. */
static void
vddk_close (void *handle)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&open_close_lock);
  struct vddk_handle *h = handle;

  VDDK_CALL_START (VixDiskLib_Close, "handle")
    VixDiskLib_Close (h->handle);
  VDDK_CALL_END (VixDiskLib_Close, 0);
  VDDK_CALL_START (VixDiskLib_Disconnect, "connection")
    VixDiskLib_Disconnect (h->connection);
  VDDK_CALL_END (VixDiskLib_Disconnect, 0);

  free_connect_params (h->params);
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

  VDDK_CALL_START (VixDiskLib_GetInfo, "handle, &info")
    err = VixDiskLib_GetInfo (h->handle, &info);
  VDDK_CALL_END (VixDiskLib_GetInfo, 0);
  if (err != VIX_OK) {
    VDDK_ERROR (err, "VixDiskLib_GetInfo");
    return -1;
  }

  size = info->capacity * (uint64_t)VIXDISKLIB_SECTOR_SIZE;

  if (vddk_debug_diskinfo) {
    nbdkit_debug ("disk info: capacity: %" PRIu64 " sectors "
                  "(%" PRIi64 " bytes)",
                  info->capacity, size);
    nbdkit_debug ("disk info: biosGeo: C:%" PRIu32 " H:%" PRIu32 " S:%" PRIu32,
                  info->biosGeo.cylinders,
                  info->biosGeo.heads,
                  info->biosGeo.sectors);
    nbdkit_debug ("disk info: physGeo: C:%" PRIu32 " H:%" PRIu32 " S:%" PRIu32,
                  info->physGeo.cylinders,
                  info->physGeo.heads,
                  info->physGeo.sectors);
    nbdkit_debug ("disk info: adapter type: %d",
                  (int) info->adapterType);
    nbdkit_debug ("disk info: num links: %d", info->numLinks);
    nbdkit_debug ("disk info: parent filename hint: %s",
                  info->parentFileNameHint ? : "NULL");
    nbdkit_debug ("disk info: uuid: %s",
                  info->uuid ? : "NULL");
    if (library_version >= 7) {
      nbdkit_debug ("disk info: sector size: "
                    "logical %" PRIu32 " physical %" PRIu32,
                    info->logicalSectorSize,
                    info->physicalSectorSize);
    }
  }

  VDDK_CALL_START (VixDiskLib_FreeInfo, "info")
    VixDiskLib_FreeInfo (info);
  VDDK_CALL_END (VixDiskLib_FreeInfo, 0);

  return (int64_t) size;
}

/* Read data from the file.
 *
 * Note that reads have to be aligned to sectors (XXX).
 */
static int
vddk_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
            uint32_t flags)
{
  struct vddk_handle *h = handle;
  VixError err;

  /* Align to sectors. */
  if (!IS_ALIGNED (offset, VIXDISKLIB_SECTOR_SIZE)) {
    nbdkit_error ("%s is not aligned to sectors", "read");
    return -1;
  }
  if (!IS_ALIGNED (count, VIXDISKLIB_SECTOR_SIZE)) {
    nbdkit_error ("%s is not aligned to sectors", "read");
    return -1;
  }
  offset /= VIXDISKLIB_SECTOR_SIZE;
  count /= VIXDISKLIB_SECTOR_SIZE;

  VDDK_CALL_START (VixDiskLib_Read,
                   "handle, %" PRIu64 " sectors, "
                   "%" PRIu32 " sectors, buffer",
                   offset, count)
    err = VixDiskLib_Read (h->handle, offset, count, buf);
  VDDK_CALL_END (VixDiskLib_Read, count * VIXDISKLIB_SECTOR_SIZE);
  if (err != VIX_OK) {
    VDDK_ERROR (err, "VixDiskLib_Read");
    return -1;
  }

  return 0;
}

static int vddk_flush (void *handle, uint32_t flags);

/* Write data to the file.
 *
 * Note that writes have to be aligned to sectors (XXX).
 */
static int
vddk_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
             uint32_t flags)
{
  const bool fua = flags & NBDKIT_FLAG_FUA;
  struct vddk_handle *h = handle;
  VixError err;

  /* Align to sectors. */
  if (!IS_ALIGNED (offset, VIXDISKLIB_SECTOR_SIZE)) {
    nbdkit_error ("%s is not aligned to sectors", "write");
    return -1;
  }
  if (!IS_ALIGNED (count, VIXDISKLIB_SECTOR_SIZE)) {
    nbdkit_error ("%s is not aligned to sectors", "write");
    return -1;
  }
  offset /= VIXDISKLIB_SECTOR_SIZE;
  count /= VIXDISKLIB_SECTOR_SIZE;

  VDDK_CALL_START (VixDiskLib_Write,
                   "handle, %" PRIu64 " sectors, "
                   "%" PRIu32 " sectors, buffer",
                   offset, count)
    err = VixDiskLib_Write (h->handle, offset, count, buf);
  VDDK_CALL_END (VixDiskLib_Write, count * VIXDISKLIB_SECTOR_SIZE);
  if (err != VIX_OK) {
    VDDK_ERROR (err, "VixDiskLib_Write");
    return -1;
  }

  if (fua) {
    if (vddk_flush (handle, 0) == -1)
      return -1;
  }

  return 0;
}

static int
vddk_can_fua (void *handle)
{
  /* The Flush call was not available in VDDK < 6.0. */
  return VixDiskLib_Flush != NULL ? NBDKIT_FUA_NATIVE : NBDKIT_FUA_NONE;
}

static int
vddk_can_flush (void *handle)
{
  /* The Flush call was not available in VDDK < 6.0. */
  return VixDiskLib_Flush != NULL;
}

/* Flush data to the file. */
static int
vddk_flush (void *handle, uint32_t flags)
{
  struct vddk_handle *h = handle;
  VixError err;

  /* The documentation for Flush is missing, but the comment in the
   * header file seems to indicate that it waits for WriteAsync
   * commands to finish.  We don't use WriteAsync, and in any case
   * there's a new function Wait to wait for those.  However I
   * verified using strace that in fact Flush does call fsync on the
   * file so it appears to be the correct call to use here.
   */

  VDDK_CALL_START (VixDiskLib_Flush, "handle")
    err = VixDiskLib_Flush (h->handle);
  VDDK_CALL_END (VixDiskLib_Flush, 0);
  if (err != VIX_OK) {
    VDDK_ERROR (err, "VixDiskLib_Flush");
    return -1;
  }

  return 0;
}

static int
vddk_can_extents (void *handle)
{
  struct vddk_handle *h = handle;
  VixError err;
  VixDiskLibBlockList *block_list;

  /* This call was added in VDDK 6.7.  In earlier versions the
   * function pointer will be NULL and we cannot query extents.
   */
  if (VixDiskLib_QueryAllocatedBlocks == NULL) {
    nbdkit_debug ("can_extents: VixDiskLib_QueryAllocatedBlocks == NULL, "
                  "probably this is VDDK < 6.7");
    return 0;
  }

  /* Suppress errors around this call.  See:
   * https://bugzilla.redhat.com/show_bug.cgi?id=1709211#c7
   */
  error_suppression = 1;

  /* However even when the call is available it rarely works well so
   * the best thing we can do here is to try the call and if it's
   * non-functional return false.
   */
  VDDK_CALL_START (VixDiskLib_QueryAllocatedBlocks,
                   "handle, 0, %d sectors, %d sectors",
                   VIXDISKLIB_MIN_CHUNK_SIZE, VIXDISKLIB_MIN_CHUNK_SIZE)
    err = VixDiskLib_QueryAllocatedBlocks (h->handle,
                                           0, VIXDISKLIB_MIN_CHUNK_SIZE,
                                           VIXDISKLIB_MIN_CHUNK_SIZE,
                                           &block_list);
  VDDK_CALL_END (VixDiskLib_QueryAllocatedBlocks, 0);
  error_suppression = 0;
  if (err == VIX_OK) {
    VDDK_CALL_START (VixDiskLib_FreeBlockList, "block_list")
      VixDiskLib_FreeBlockList (block_list);
    VDDK_CALL_END (VixDiskLib_FreeBlockList, 0);
  }
  if (err != VIX_OK) {
    char *errmsg = VixDiskLib_GetErrorText (err, NULL);
    nbdkit_debug ("can_extents: VixDiskLib_QueryAllocatedBlocks test failed, "
                  "extents support will be disabled: "
                  "original error: %s",
                  errmsg);
    VixDiskLib_FreeErrorText (errmsg);
    return 0;
  }

  return 1;
}

static int
add_extent (struct nbdkit_extents *extents,
            uint64_t *position, uint64_t next_position, bool is_hole)
{
  uint32_t type = 0;
  const uint64_t length = next_position - *position;

  if (is_hole) {
    type = NBDKIT_EXTENT_HOLE;
    /* Images opened as single link might be backed by another file in the
       chain, so the holes are not guaranteed to be zeroes. */
    if (!single_link)
      type |= NBDKIT_EXTENT_ZERO;
  }

  assert (*position <= next_position);
  if (*position == next_position)
    return 0;

  if (vddk_debug_extents)
    nbdkit_debug ("adding extent type %s at [%" PRIu64 "...%" PRIu64 "]",
                  is_hole ? "hole" : "allocated data",
                  *position, next_position-1);
  if (nbdkit_add_extent (extents, *position, length, type) == -1)
    return -1;

  *position = next_position;
  return 0;
}

static int
vddk_extents (void *handle, uint32_t count, uint64_t offset, uint32_t flags,
              struct nbdkit_extents *extents)
{
  struct vddk_handle *h = handle;
  bool req_one = flags & NBDKIT_FLAG_REQ_ONE;
  uint64_t position, end, start_sector;

  position = offset;
  end = offset + count;

  /* We can only query whole chunks.  Therefore start with the first
   * chunk before offset.
   */
  start_sector =
    ROUND_DOWN (offset, VIXDISKLIB_MIN_CHUNK_SIZE * VIXDISKLIB_SECTOR_SIZE)
    / VIXDISKLIB_SECTOR_SIZE;
  while (start_sector * VIXDISKLIB_SECTOR_SIZE < end) {
    VixError err;
    uint32_t i;
    uint64_t nr_chunks, nr_sectors;
    VixDiskLibBlockList *block_list;

    assert (IS_ALIGNED (start_sector, VIXDISKLIB_MIN_CHUNK_SIZE));

    nr_chunks =
      ROUND_UP (end - start_sector * VIXDISKLIB_SECTOR_SIZE,
                VIXDISKLIB_MIN_CHUNK_SIZE * VIXDISKLIB_SECTOR_SIZE)
      / (VIXDISKLIB_MIN_CHUNK_SIZE * VIXDISKLIB_SECTOR_SIZE);
    nr_chunks = MIN (nr_chunks, VIXDISKLIB_MAX_CHUNK_NUMBER);
    nr_sectors = nr_chunks * VIXDISKLIB_MIN_CHUNK_SIZE;

    VDDK_CALL_START (VixDiskLib_QueryAllocatedBlocks,
                     "handle, %" PRIu64 " sectors, %" PRIu64 " sectors, "
                     "%d sectors",
                     start_sector, nr_sectors, VIXDISKLIB_MIN_CHUNK_SIZE)
      err = VixDiskLib_QueryAllocatedBlocks (h->handle,
                                             start_sector, nr_sectors,
                                             VIXDISKLIB_MIN_CHUNK_SIZE,
                                             &block_list);
    VDDK_CALL_END (VixDiskLib_QueryAllocatedBlocks, 0);
    if (err != VIX_OK) {
      VDDK_ERROR (err, "VixDiskLib_QueryAllocatedBlocks");
      return -1;
    }

    for (i = 0; i < block_list->numBlocks; ++i) {
      uint64_t blk_offset, blk_length;

      blk_offset = block_list->blocks[i].offset * VIXDISKLIB_SECTOR_SIZE;
      blk_length = block_list->blocks[i].length * VIXDISKLIB_SECTOR_SIZE;

      /* The query returns allocated blocks.  We must insert holes
       * between the blocks as necessary.
       */
      if ((position < blk_offset &&
           add_extent (extents, &position, blk_offset, true) == -1) ||
          (add_extent (extents,
                       &position, blk_offset + blk_length, false) == -1)) {
        VDDK_CALL_START (VixDiskLib_FreeBlockList, "block_list")
          VixDiskLib_FreeBlockList (block_list);
        VDDK_CALL_END (VixDiskLib_FreeBlockList, 0);
        return -1;
      }
    }
    VDDK_CALL_START (VixDiskLib_FreeBlockList, "block_list")
      VixDiskLib_FreeBlockList (block_list);
    VDDK_CALL_END (VixDiskLib_FreeBlockList, 0);

    /* There's an implicit hole after the returned list of blocks, up
     * to the end of the QueryAllocatedBlocks request.
     */
    if (add_extent (extents,
                    &position,
                    (start_sector + nr_sectors) * VIXDISKLIB_SECTOR_SIZE,
                    true) == -1)
      return -1;

    start_sector += nr_sectors;

    /* If one extent was requested, as long as we've added an extent
     * overlapping the original offset we're done.
     */
    if (req_one && position > offset)
      break;
  }

  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "vddk",
  .longname          = "VMware VDDK plugin",
  .version           = PACKAGE_VERSION,
  .unload            = vddk_unload,
  .config            = vddk_config,
  .config_complete   = vddk_config_complete,
  .config_help       = vddk_config_help,
  .magic_config_key  = "file",
  .dump_plugin       = vddk_dump_plugin,
  .get_ready         = vddk_get_ready,
  .after_fork        = vddk_after_fork,
  .open              = vddk_open,
  .close             = vddk_close,
  .get_size          = vddk_get_size,
  .pread             = vddk_pread,
  .pwrite            = vddk_pwrite,
  .can_fua           = vddk_can_fua,
  .can_flush         = vddk_can_flush,
  .flush             = vddk_flush,
  .can_extents       = vddk_can_extents,
  .extents           = vddk_extents,
};

NBDKIT_REGISTER_PLUGIN(plugin)
