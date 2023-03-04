/* nbdkit
 * Copyright Red Hat
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
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "utils.h"
#include "vector.h"

/* The script name. */
static char *script;
static bool unlink_on_exit = false;

/* C compiler and flags. */
static const char *cc = CC;
static const char *cflags =
  CFLAGS
  " -fPIC -shared"
#ifdef __APPLE__
  " -Wl,-undefined,dynamic_lookup"
#endif
  ;
static const char *extra_cflags;

/* List of parameters for the subplugin. */
struct key_value { const char *key, *value; };
DEFINE_VECTOR_TYPE (params_vector, struct key_value);
static params_vector params = empty_vector;

/* The subplugin. */
static void *dl;
static struct nbdkit_plugin subplugin;

static void
cc_unload (void)
{
  if (subplugin.unload)
    subplugin.unload ();

  if (unlink_on_exit)
    unlink (script);
  if (dl)
    dlclose (dl);
  free (params.ptr);
  free (script);
}

static void
cc_dump_plugin (void)
{
  printf ("CC=%s\n", CC);
  printf ("CFLAGS=%s\n", CFLAGS);
}

#define cc_config_help \
  "[script=]<FILENAME>   (required) The shell script to run.\n" \
  "CC=<CC>                          C compiler.\n" \
  "CFLAGS=<CFLAGS>                  C compiler flags.\n" \
  "EXTRA_CFLAGS=<CFLAGS>            Extra C compiler flags.\n" \
  "[other arguments may be used by the plugin that you load]"

static char *
inline_script (void)
{
  CLEANUP_FREE char *cmd = NULL;
  int fd;

  if (!nbdkit_stdio_safe ()) {
    nbdkit_error ("inline script is incompatible with -s");
    return NULL;
  }

  script = strdup ("/tmp/ccXXXXXX.c");
  if (script == NULL) {
    nbdkit_error ("strdup: %m");
    return NULL;
  }

  fd = mkstemps (script, 2);
  if (fd == -1) {
    nbdkit_error ("mkstemps: %m");
    return NULL;
  }
  close (fd);
  unlink_on_exit = true;

  if (asprintf (&cmd, "cat > %s", script) == -1) {
    nbdkit_error ("asprintf: %m");
    return NULL;
  }

  if (system (cmd) != 0) {
    nbdkit_error ("cc: failed to copy inline script to temporary file");
    return NULL;
  }

  return script;
}

static int
cc_config (const char *key, const char *value)
{
  if (!script) {
    /* The first parameter must be "script". */
    if (strcmp (key, "script") != 0) {
      nbdkit_error ("cc: the first parameter must be the C file or \"-\"");
      return -1;
    }
    if (strcmp (value, "-") != 0)
      script = nbdkit_realpath (value);
    else
      script = inline_script ();
    if (script == NULL)
      return -1;

    return 0;
  }
  else {
    /* Although not impossible, it's likely to be a bug if there is a
     * further parameter called "script" so disallow it.
     */
    if (strcmp (key, "script") == 0) {
      nbdkit_error ("cc: script parameter must appear only once");
      return -1;
    }
    /* Otherwise parse our parameters. */
    else if (strcmp (key, "CC") == 0)
      cc = value;
    else if (strcmp (key, "CFLAGS") == 0)
      cflags = value;
    else if (strcmp (key, "EXTRA_CFLAGS") == 0)
      extra_cflags = value;
    else {
      /* Anything else is saved for the subplugin. */
      struct key_value kv = { .key = key, .value = value };

      if (params_vector_append (&params, kv) == -1) {
        nbdkit_error ("realloc: %m");
        return -1;
      }
    }
    return 0;
  }
}

/* We must compile and load the subplugin here (not in get_ready)
 * because we must find the subplugin's thread model, and the core
 * server will query that straight after config_complete.
 */
static int
cc_config_complete (void)
{
  CLEANUP_FREE char *command = NULL;
  size_t len = 0, size, i;
  FILE *fp;
  int fd, r;
  char tmpfile[] = "/tmp/ccXXXXXX.so";
  struct nbdkit_plugin *(*plugin_init) (void);
  char *error;
  const struct nbdkit_plugin *ptr;

  if (!script) {
    nbdkit_error ("cc: no C program name (or \"-\") given");
    return -1;
  }

  /* Create a temporary file to store the compiled plugin. */
  fd = mkstemps (tmpfile, 3);
  if (fd == -1) {
    nbdkit_error ("mkstemps: %m");
    return -1;
  }
  close (fd);

  /* Compile the C program. */
  fp = open_memstream (&command, &len);
  if (fp == NULL) {
    nbdkit_error ("open_memstream: %m");
    return -1;
  }
  /* The C compiler and C flags don't need to be quoted. */
  fprintf (fp, "%s %s ", cc, cflags);
  if (extra_cflags)
    fprintf (fp, "%s ", extra_cflags);
  shell_quote (script, fp);
  fprintf (fp, " -o ");
  shell_quote (tmpfile, fp);
  if (fclose (fp) == EOF) {
    nbdkit_error ("memstream failed: %m");
    return -1;
  }

  nbdkit_debug ("cc: %s", command);
  r = system (command);
  if (exit_status_to_nbd_error (r, cc) == -1) {
    unlink (tmpfile);
    return -1;
  }

  /* Load the subplugin. */
  dl = dlopen (tmpfile, RTLD_NOW);
  unlink (tmpfile);
  if (dl == NULL) {
    nbdkit_error ("cannot open the compiled plugin: %s", dlerror ());
    return -1;
  }

  /* Now we basically behave like core nbdkit when it loads a plugin. */
  dlerror ();
  *(void **) (&plugin_init) = dlsym (dl, "plugin_init");
  if ((error = dlerror ()) != NULL) {
    nbdkit_error ("no plugin_init symbol found: %s", error);
    return -1;
  }
  if (!plugin_init) {
    nbdkit_error ("invalid plugin_init symbol");
    return -1;
  }
  ptr = plugin_init ();
  if (!ptr) {
    nbdkit_error ("plugin registration failed");
    return -1;
  }

  /* Are the APIs compatible? */
  if (ptr->_api_version != NBDKIT_API_VERSION) {
    nbdkit_error ("plugin uses the wrong NBDKIT_API_VERSION, it must be %d",
                  NBDKIT_API_VERSION);
    return -1;
  }

  /* Copy the subplugins struct into our plugin global, padding or
   * truncating as necessary.
   */
  size = sizeof subplugin;      /* Size of our struct. */
  if (size > ptr->_struct_size)
    size = ptr->_struct_size;
  memcpy (&subplugin, ptr, size);

  /* Check that the plugin has .open, .get_size and .pread. */
  if (subplugin.open == NULL) {
    nbdkit_error ("plugin must have a .open callback");
    return -1;
  }
  if (subplugin.get_size == NULL) {
    nbdkit_error ("plugin must have a .get_size callback");
    return -1;
  }
  if (subplugin.pread == NULL) {
    nbdkit_error ("plugin must have a .pread callback");
    return -1;
  }

  /* Now we have to call the subplugin's load, config and
   * config_complete.  Everything after that will be called via the
   * core server through our forwarding functions below.
   */
  if (subplugin.load)
    subplugin.load ();
  if (subplugin.config) {
    for (i = 0; i < params.len; ++i) {
      if (subplugin.config (params.ptr[i].key, params.ptr[i].value) == -1)
        return -1;
    }
  }
  else if (params.len > 0) {
    /* Just print the first one in the error message. */
    nbdkit_error ("unknown parameter: %s", params.ptr[0].key);
    return -1;
  }
  if (subplugin.config_complete) {
    if (subplugin.config_complete () == -1)
      return -1;
  }

  return 0;
}

/* This is adjusted when we load the subplugin. */
#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Set the thread model from the subplugin. */
static int
cc_thread_model (void)
{
  if (subplugin.thread_model != NULL)
    return subplugin.thread_model ();
  else
    return subplugin._thread_model;
}

/* All other calls are forwarded to the subplugin. */
static int
cc_get_ready (void)
{
  if (subplugin.get_ready)
    return subplugin.get_ready ();
  return 0;
}

static int
cc_after_fork (void)
{
  if (subplugin.after_fork)
    return subplugin.after_fork ();
  return 0;
}

static int
cc_preconnect (int readonly)
{
  if (subplugin.preconnect)
    return subplugin.preconnect (readonly);
  return 0;
}

static int
cc_list_exports (int readonly, int is_tls, struct nbdkit_exports *exports)
{
  if (subplugin.list_exports)
    return subplugin.list_exports (readonly, is_tls, exports);
  return nbdkit_use_default_export (exports);
}

static const char *
cc_default_export (int readonly, int is_tls)
{
  if (subplugin.default_export)
    return subplugin.default_export (readonly, is_tls);
  return "";
}

static void *
cc_open (int readonly)
{
  return subplugin.open (readonly);
}

static void
cc_close (void *handle)
{
  if (subplugin.close)
    subplugin.close (handle);
}

static const char *
cc_export_description (void *handle)
{
  if (subplugin.export_description)
    return subplugin.export_description (handle);
  return NULL;
}

static int64_t
cc_get_size (void *handle)
{
  return subplugin.get_size (handle);
}

static int
cc_block_size (void *handle,
               uint32_t *minimum, uint32_t *preferred, uint32_t *maximum)
{
  if (subplugin.block_size)
    return subplugin.block_size (handle, minimum, preferred, maximum);
  else {
    *minimum = *preferred = *maximum = 0;
    return 0;
  }
}

static int
cc_can_write (void *handle)
{
  if (subplugin.can_write)
    return subplugin.can_write (handle);
  else
    return !!subplugin.pwrite;
}

static int
cc_can_flush (void *handle)
{
  if (subplugin.can_flush)
    return subplugin.can_flush (handle);
  else
    return !!subplugin.flush;
}

static int
cc_is_rotational (void *handle)
{
  if (subplugin.is_rotational)
    return subplugin.is_rotational (handle);
  else
    return 0;
}

static int
cc_can_trim (void *handle)
{
  if (subplugin.can_trim)
    return subplugin.can_trim (handle);
  else
    return !!subplugin.trim;
}

static int
cc_can_zero (void *handle)
{
  if (subplugin.can_zero)
    return subplugin.can_zero (handle);
  else
    return !!subplugin.zero;
}

static int
cc_can_fast_zero (void *handle)
{
  if (subplugin.can_fast_zero)
    return subplugin.can_fast_zero (handle);
  else
    return 0;
}

static int
cc_can_extents (void *handle)
{
  if (subplugin.can_extents)
    return subplugin.can_extents (handle);
  else
    return !!subplugin.extents;
}

static int
cc_can_fua (void *handle)
{
  if (subplugin.can_fua)
    return subplugin.can_fua (handle);
  else if (cc_can_flush (handle))
    return NBDKIT_FUA_EMULATE;
  else
    return NBDKIT_FUA_NONE;
}

static int
cc_can_multi_conn (void *handle)
{
  if (subplugin.can_multi_conn)
    return subplugin.can_multi_conn (handle);
  else
    return 0;
}

static int
cc_can_cache (void *handle)
{
  if (subplugin.can_cache)
    return subplugin.can_cache (handle);
  else if (subplugin.cache)
    return NBDKIT_CACHE_NATIVE;
  else
    return NBDKIT_CACHE_NONE;
}

static int
cc_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
          uint32_t flags)
{
  return subplugin.pread (handle, buf, count, offset, flags);
}

static int
cc_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
           uint32_t flags)
{
  if (subplugin.pwrite)
    return subplugin.pwrite (handle, buf, count, offset, flags);
  else {
    nbdkit_error ("missing %s callback", "pwrite");
    errno = EROFS;
    return -1;
  }
}

static int
cc_flush (void *handle, uint32_t flags)
{
  if (subplugin.flush)
    return subplugin.flush (handle, flags);
  else {
    nbdkit_error ("missing %s callback", "flush");
    errno = EINVAL;
    return -1;
  }
}

static int
cc_trim (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  if (subplugin.trim)
    return subplugin.trim (handle, count, offset, flags);
  else {
    nbdkit_error ("missing %s callback", "trim");
    errno = EINVAL;
    return -1;
  }
}

static int
cc_zero (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  if (subplugin.zero)
    return subplugin.zero (handle, count, offset, flags);
  else {
    /* Inform nbdkit to fall back to pwrite. */
    nbdkit_error ("missing %s callback", "zero");
    errno = EOPNOTSUPP;
    return -1;
  }
}

static int
cc_extents (void *handle, uint32_t count, uint64_t offset,
            uint32_t flags, struct nbdkit_extents *extents)
{
  if (subplugin.extents)
    return subplugin.extents (handle, count, offset, flags, extents);
  else {
    nbdkit_error ("missing %s callback", "extents");
    errno = EINVAL;
    return -1;
  }
}

static int
cc_cache (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  if (subplugin.cache)
    return subplugin.cache (handle, count, offset, flags);
  else
    /* A plugin may advertise caching but not provide .cache; in that
     * case, caching is explicitly a no-op.
     */
    return 0;
}

static void
cc_cleanup (void)
{
  if (subplugin.cleanup)
    subplugin.cleanup ();
}

static struct nbdkit_plugin plugin = {
  .name              = "cc",
  .longname          = "nbdkit C compiler plugin",
  .version           = PACKAGE_VERSION,

  /* These are the callbacks that this plugin overrides. */
  .unload            = cc_unload,
  .dump_plugin       = cc_dump_plugin,
  .config            = cc_config,
  .config_complete   = cc_config_complete,
  .config_help       = cc_config_help,
  .thread_model      = cc_thread_model,

  /* And we must provide callbacks for everything else, which are
   * passed through to the subplugin.
   */
  .get_ready          = cc_get_ready,
  .after_fork         = cc_after_fork,
  .cleanup            = cc_cleanup,

  .preconnect         = cc_preconnect,
  .list_exports       = cc_list_exports,
  .default_export     = cc_default_export,
  .open               = cc_open,
  .close              = cc_close,

  .export_description = cc_export_description,
  .get_size           = cc_get_size,
  .block_size         = cc_block_size,
  .can_write          = cc_can_write,
  .can_flush          = cc_can_flush,
  .is_rotational      = cc_is_rotational,
  .can_trim           = cc_can_trim,
  .can_zero           = cc_can_zero,
  .can_fast_zero      = cc_can_fast_zero,
  .can_extents        = cc_can_extents,
  .can_fua            = cc_can_fua,
  .can_multi_conn     = cc_can_multi_conn,
  .can_cache          = cc_can_cache,

  .pread              = cc_pread,
  .pwrite             = cc_pwrite,
  .flush              = cc_flush,
  .trim               = cc_trim,
  .zero               = cc_zero,
  .extents            = cc_extents,
  .cache              = cc_cache,

  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN (plugin)
