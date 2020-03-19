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
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>

#include "internal.h"
#include "minmax.h"

/* We extend the generic backend struct with extra fields relating
 * to this plugin.
 */
struct backend_plugin {
  struct backend backend;
  struct nbdkit_plugin plugin;
};

static void
plugin_free (struct backend *b)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  backend_unload (b, p->plugin.unload);
  free (p);
}

static int
plugin_thread_model (struct backend *b)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  int model = p->plugin._thread_model;
  int r;

#if !(defined SOCK_CLOEXEC && defined HAVE_MKOSTEMP && defined HAVE_PIPE2 && \
      defined HAVE_ACCEPT4)
  if (model > NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS) {
    debug ("system lacks atomic CLOEXEC, serializing to avoid fd leaks");
    model = NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS;
  }
#endif

  if (p->plugin.thread_model) {
    r = p->plugin.thread_model ();
    if (r == -1)
      exit (EXIT_FAILURE);
    if (r < model)
      model = r;
  }

  return model;
}

static const char *
plugin_name (struct backend *b)
{
  return b->name;
}

static void
plugin_usage (struct backend *b)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  const char *t;

  printf ("plugin: %s", b->name);
  if (p->plugin.longname)
    printf (" (%s)", p->plugin.longname);
  printf ("\n");
  printf ("(%s)\n", b->filename);
  if (p->plugin.description) {
    printf ("%s", p->plugin.description);
    if ((t = strrchr (p->plugin.description, '\n')) == NULL || t[1])
      printf ("\n");
  }
  if (p->plugin.config_help) {
    printf ("%s", p->plugin.config_help);
    if ((t = strrchr (p->plugin.config_help, '\n')) == NULL || t[1])
      printf ("\n");
  }
}

static const char *
plugin_version (struct backend *b)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  return p->plugin.version;
}

/* This implements the --dump-plugin option. */
static void
plugin_dump_fields (struct backend *b)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  char *path;

  path = nbdkit_realpath (b->filename);
  printf ("path=%s\n", path);
  free (path);

  printf ("name=%s\n", b->name);
  if (p->plugin.version)
    printf ("version=%s\n", p->plugin.version);

  printf ("api_version=%d\n", p->plugin._api_version);
  printf ("struct_size=%" PRIu64 "\n", p->plugin._struct_size);
  printf ("max_thread_model=%s\n",
          name_of_thread_model (p->plugin._thread_model));
  printf ("thread_model=%s\n",
          name_of_thread_model (top->thread_model (top)));
  printf ("errno_is_preserved=%d\n", !!p->plugin.errno_is_preserved);
  if (p->plugin.magic_config_key)
    printf ("magic_config_key=%s\n", p->plugin.magic_config_key);

#define HAS(field) if (p->plugin.field) printf ("has_%s=1\n", #field)
  HAS (longname);
  HAS (description);
  HAS (load);
  HAS (unload);
  HAS (dump_plugin);
  HAS (config);
  HAS (config_complete);
  HAS (config_help);
  HAS (get_ready);
  HAS (preconnect);
  HAS (open);
  HAS (close);
  HAS (get_size);
  HAS (can_write);
  HAS (can_flush);
  HAS (is_rotational);
  HAS (can_trim);
  HAS (_pread_v1);
  HAS (_pwrite_v1);
  HAS (_flush_v1);
  HAS (_trim_v1);
  HAS (_zero_v1);
  HAS (can_zero);
  HAS (can_fua);
  HAS (pread);
  HAS (pwrite);
  HAS (flush);
  HAS (trim);
  HAS (zero);
  HAS (can_multi_conn);
  HAS (can_extents);
  HAS (extents);
  HAS (can_cache);
  HAS (cache);
  HAS (thread_model);
  HAS (can_fast_zero);
#undef HAS

  /* Custom fields. */
  if (p->plugin.dump_plugin)
    p->plugin.dump_plugin ();
}

static void
plugin_config (struct backend *b, const char *key, const char *value)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  debug ("%s: config key=%s, value=%s", b->name, key, value);

  if (p->plugin.config == NULL) {
    fprintf (stderr,
             "%s: %s: this plugin does not need command line configuration\n"
             "Try using: %s --help %s\n",
             program_name, b->filename,
             program_name, b->filename);
    exit (EXIT_FAILURE);
  }

  if (p->plugin.config (key, value) == -1)
    exit (EXIT_FAILURE);
}

static void
plugin_config_complete (struct backend *b)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  debug ("%s: config_complete", b->name);

  if (!p->plugin.config_complete)
    return;

  if (p->plugin.config_complete () == -1)
    exit (EXIT_FAILURE);
}

static const char *
plugin_magic_config_key (struct backend *b)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  return p->plugin.magic_config_key;
}

static void
plugin_get_ready (struct backend *b)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  debug ("%s: get_ready", b->name);

  if (!p->plugin.get_ready)
    return;

  if (p->plugin.get_ready () == -1)
    exit (EXIT_FAILURE);
}

static int
plugin_preconnect (struct backend *b, int readonly)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  debug ("%s: preconnect", b->name);

  if (!p->plugin.preconnect)
    return 0;

  return p->plugin.preconnect (readonly);
}

static void *
plugin_open (struct backend *b, int readonly)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  assert (p->plugin.open != NULL);

  return p->plugin.open (readonly);
}

/* We don't expose .prepare and .finalize to plugins since they aren't
 * necessary.  Plugins can easily do the same work in .open and
 * .close.
 */
static int
plugin_prepare (struct backend *b, void *handle,
                int readonly)
{
  return 0;
}

static int
plugin_finalize (struct backend *b, void *handle)
{
  return 0;
}

static void
plugin_close (struct backend *b, void *handle)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  if (handle && p->plugin.close)
    p->plugin.close (handle);
}

static int64_t
plugin_get_size (struct backend *b, void *handle)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  assert (p->plugin.get_size != NULL);

  return p->plugin.get_size (handle);
}

static int
normalize_bool (int value)
{
  if (value == -1 || value == 0)
    return value;
  /* Normalize all other non-zero values to true */
  return 1;
}

static int
plugin_can_write (struct backend *b, void *handle)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  if (p->plugin.can_write)
    return normalize_bool (p->plugin.can_write (handle));
  else
    return p->plugin.pwrite || p->plugin._pwrite_v1;
}

static int
plugin_can_flush (struct backend *b, void *handle)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  if (p->plugin.can_flush)
    return normalize_bool (p->plugin.can_flush (handle));
  else
    return p->plugin.flush || p->plugin._flush_v1;
}

static int
plugin_is_rotational (struct backend *b, void *handle)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  if (p->plugin.is_rotational)
    return normalize_bool (p->plugin.is_rotational (handle));
  else
    return 0; /* assume false */
}

static int
plugin_can_trim (struct backend *b, void *handle)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  if (p->plugin.can_trim)
    return normalize_bool (p->plugin.can_trim (handle));
  else
    return p->plugin.trim || p->plugin._trim_v1;
}

static int
plugin_can_zero (struct backend *b, void *handle)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  int r;

  /* Note the special case here: the plugin's .can_zero returns a bool
   * which controls only whether we call .zero; while the backend
   * expects .can_zero to return a tri-state on level of support.
   */
  if (p->plugin.can_zero) {
    r = p->plugin.can_zero (handle);
    if (r == -1)
      return -1;
    return r ? NBDKIT_ZERO_NATIVE : NBDKIT_ZERO_EMULATE;
  }
  if (p->plugin.zero || p->plugin._zero_v1)
    return NBDKIT_ZERO_NATIVE;
  return NBDKIT_ZERO_EMULATE;
}

static int
plugin_can_fast_zero (struct backend *b, void *handle)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  int r;

  if (p->plugin.can_fast_zero)
    return normalize_bool (p->plugin.can_fast_zero (handle));
  /* Advertise support for fast zeroes if no .zero or .can_zero is
   * false: in those cases, we fail fast instead of using .pwrite.
   * This also works when v1 plugin has only ._zero_v1.
   */
  if (p->plugin.zero == NULL)
    return 1;
  r = backend_can_zero (b);
  if (r == -1)
    return -1;
  return !r;
}

static int
plugin_can_extents (struct backend *b, void *handle)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  if (p->plugin.can_extents)
    return normalize_bool (p->plugin.can_extents (handle));
  else
    return p->plugin.extents != NULL;
}

static int
plugin_can_fua (struct backend *b, void *handle)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  int r;

  /* The plugin must use API version 2 and have .can_fua return
     NBDKIT_FUA_NATIVE before we will pass the FUA flag on. */
  if (p->plugin.can_fua) {
    r = p->plugin.can_fua (handle);
    if (r > NBDKIT_FUA_EMULATE && p->plugin._api_version == 1)
      r = NBDKIT_FUA_EMULATE;
    return r;
  }
  /* We intend to call .flush even if .can_flush returns false. */
  if (p->plugin.flush || p->plugin._flush_v1)
    return NBDKIT_FUA_EMULATE;
  return NBDKIT_FUA_NONE;
}

static int
plugin_can_multi_conn (struct backend *b, void *handle)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  if (p->plugin.can_multi_conn)
    return normalize_bool (p->plugin.can_multi_conn (handle));
  else
    return 0; /* assume false */
}

static int
plugin_can_cache (struct backend *b, void *handle)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  if (p->plugin.can_cache)
    return p->plugin.can_cache (handle);
  if (p->plugin.cache)
    return NBDKIT_CACHE_NATIVE;
  return NBDKIT_CACHE_NONE;
}

/* Plugins and filters can call this to set the true errno, in cases
 * where !errno_is_preserved.
 */
void
nbdkit_set_error (int err)
{
  threadlocal_set_error (err);
}

/* Grab the appropriate error value.
 */
static int
get_error (struct backend_plugin *p)
{
  int ret = threadlocal_get_error ();

  if (!ret && p->plugin.errno_is_preserved != 0)
    ret = errno;
  return ret ? ret : EIO;
}

static int
plugin_pread (struct backend *b, void *handle,
              void *buf, uint32_t count, uint64_t offset, uint32_t flags,
              int *err)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  int r;

  assert (p->plugin.pread || p->plugin._pread_v1);

  if (p->plugin.pread)
    r = p->plugin.pread (handle, buf, count, offset, 0);
  else
    r = p->plugin._pread_v1 (handle, buf, count, offset);
  if (r == -1)
    *err = get_error (p);
  return r;
}

static int
plugin_flush (struct backend *b, void *handle,
              uint32_t flags, int *err)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  int r;

  if (p->plugin.flush)
    r = p->plugin.flush (handle, 0);
  else if (p->plugin._flush_v1)
    r = p->plugin._flush_v1 (handle);
  else {
    *err = EINVAL;
    return -1;
  }
  if (r == -1)
    *err = get_error (p);
  return r;
}

static int
plugin_pwrite (struct backend *b, void *handle,
               const void *buf, uint32_t count, uint64_t offset, uint32_t flags,
               int *err)
{
  int r;
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  bool fua = flags & NBDKIT_FLAG_FUA;
  bool need_flush = false;

  if (fua && backend_can_fua (b) != NBDKIT_FUA_NATIVE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }
  if (p->plugin.pwrite)
    r = p->plugin.pwrite (handle, buf, count, offset, flags);
  else if (p->plugin._pwrite_v1)
    r = p->plugin._pwrite_v1 (handle, buf, count, offset);
  else {
    *err = EROFS;
    return -1;
  }
  if (r != -1 && need_flush)
    r = plugin_flush (b, handle, 0, err);
  if (r == -1 && !*err)
    *err = get_error (p);
  return r;
}

static int
plugin_trim (struct backend *b, void *handle,
             uint32_t count, uint64_t offset, uint32_t flags, int *err)
{
  int r;
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  bool fua = flags & NBDKIT_FLAG_FUA;
  bool need_flush = false;

  if (fua && backend_can_fua (b) != NBDKIT_FUA_NATIVE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }
  if (p->plugin.trim)
    r = p->plugin.trim (handle, count, offset, flags);
  else if (p->plugin._trim_v1)
    r = p->plugin._trim_v1 (handle, count, offset);
  else {
    *err = EINVAL;
    return -1;
  }
  if (r != -1 && need_flush)
    r = plugin_flush (b, handle, 0, err);
  if (r == -1 && !*err)
    *err = get_error (p);
  return r;
}

static int
plugin_zero (struct backend *b, void *handle,
             uint32_t count, uint64_t offset, uint32_t flags, int *err)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  int r = -1;
  bool may_trim = flags & NBDKIT_FLAG_MAY_TRIM;
  bool fua = flags & NBDKIT_FLAG_FUA;
  bool fast_zero = flags & NBDKIT_FLAG_FAST_ZERO;
  bool emulate = false;
  bool need_flush = false;

  if (fua && backend_can_fua (b) != NBDKIT_FUA_NATIVE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }
  if (!count)
    return 0;

  if (backend_can_zero (b) == NBDKIT_ZERO_NATIVE) {
    errno = 0;
    if (p->plugin.zero)
      r = p->plugin.zero (handle, count, offset, flags);
    else if (p->plugin._zero_v1) {
      if (fast_zero) {
        *err = EOPNOTSUPP;
        return -1;
      }
      r = p->plugin._zero_v1 (handle, count, offset, may_trim);
    }
    else
      emulate = true;
    if (r == -1)
      *err = emulate ? EOPNOTSUPP : get_error (p);
    if (r == 0 || (*err != EOPNOTSUPP && *err != ENOTSUP))
      goto done;
  }

  if (fast_zero) {
    assert (r == -1);
    *err = EOPNOTSUPP;
    goto done;
  }

  assert (p->plugin.pwrite || p->plugin._pwrite_v1);
  flags &= ~NBDKIT_FLAG_MAY_TRIM;
  threadlocal_set_error (0);
  *err = 0;

  while (count) {
    /* Always contains zeroes, but we can't use const or else gcc 9
     * will use .rodata instead of .bss and inflate the binary size.
     */
    static /* const */ char buf[MAX_REQUEST_SIZE];
    uint32_t limit = MIN (count, sizeof buf);

    r = plugin_pwrite (b, handle, buf, limit, offset, flags, err);
    if (r == -1)
      break;
    count -= limit;
  }

 done:
  if (r != -1 && need_flush)
    r = plugin_flush (b, handle, 0, err);
  if (r == -1 && !*err)
    *err = get_error (p);
  return r;
}

static int
plugin_extents (struct backend *b, void *handle,
                uint32_t count, uint64_t offset, uint32_t flags,
                struct nbdkit_extents *extents, int *err)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  int r;

  /* This should be true because plugin_can_extents checks it. */
  assert (p->plugin.extents);

  r = p->plugin.extents (handle, count, offset, flags, extents);
  if (r >= 0 && nbdkit_extents_count (extents) < 1) {
    nbdkit_error ("extents: plugin must return at least one extent");
    nbdkit_set_error (EINVAL);
    r = -1;
  }
  if (r == -1)
    *err = get_error (p);
  return r;
}

static int
plugin_cache (struct backend *b, void *handle,
              uint32_t count, uint64_t offset, uint32_t flags,
              int *err)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  int r;

  /* A plugin may advertise caching but not provide .cache; in that
   * case, caching is explicitly a no-op. */
  if (!p->plugin.cache)
    return 0;

  r = p->plugin.cache (handle, count, offset, flags);
  if (r == -1)
    *err = get_error (p);
  return r;
}

static struct backend plugin_functions = {
  .free = plugin_free,
  .thread_model = plugin_thread_model,
  .plugin_name = plugin_name,
  .usage = plugin_usage,
  .version = plugin_version,
  .dump_fields = plugin_dump_fields,
  .config = plugin_config,
  .config_complete = plugin_config_complete,
  .magic_config_key = plugin_magic_config_key,
  .get_ready = plugin_get_ready,
  .preconnect = plugin_preconnect,
  .open = plugin_open,
  .prepare = plugin_prepare,
  .finalize = plugin_finalize,
  .close = plugin_close,
  .get_size = plugin_get_size,
  .can_write = plugin_can_write,
  .can_flush = plugin_can_flush,
  .is_rotational = plugin_is_rotational,
  .can_trim = plugin_can_trim,
  .can_zero = plugin_can_zero,
  .can_fast_zero = plugin_can_fast_zero,
  .can_extents = plugin_can_extents,
  .can_fua = plugin_can_fua,
  .can_multi_conn = plugin_can_multi_conn,
  .can_cache = plugin_can_cache,
  .pread = plugin_pread,
  .pwrite = plugin_pwrite,
  .flush = plugin_flush,
  .trim = plugin_trim,
  .zero = plugin_zero,
  .extents = plugin_extents,
  .cache = plugin_cache,
};

/* Register and load a plugin. */
struct backend *
plugin_register (size_t index, const char *filename,
                 void *dl, struct nbdkit_plugin *(*plugin_init) (void))
{
  struct backend_plugin *p;
  const struct nbdkit_plugin *plugin;
  size_t size;

  p = malloc (sizeof *p);
  if (p == NULL) {
    perror ("strdup");
    exit (EXIT_FAILURE);
  }

  p->backend = plugin_functions;
  backend_init (&p->backend, NULL, index, filename, dl, "plugin");

  /* Call the initialization function which returns the address of the
   * plugin's own 'struct nbdkit_plugin'.
   */
  plugin = plugin_init ();
  if (!plugin) {
    fprintf (stderr, "%s: %s: plugin registration function failed\n",
             program_name, filename);
    exit (EXIT_FAILURE);
  }

  /* Check for incompatible future versions. */
  if (plugin->_api_version < 0 || plugin->_api_version > 2) {
    fprintf (stderr,
             "%s: %s: plugin is incompatible with this version of nbdkit "
             "(_api_version = %d)\n",
             program_name, filename, plugin->_api_version);
    exit (EXIT_FAILURE);
  }

  /* Since the plugin might be much older than the current version of
   * nbdkit, only copy up to the self-declared _struct_size of the
   * plugin and zero out the rest.  If the plugin is much newer then
   * we'll only call the "old" fields.
   */
  size = sizeof p->plugin;      /* our struct */
  memset (&p->plugin, 0, size);
  if (size > plugin->_struct_size)
    size = plugin->_struct_size;
  memcpy (&p->plugin, plugin, size);

  /* Check for the minimum fields which must exist in the
   * plugin struct.
   */
  if (p->plugin.open == NULL) {
    fprintf (stderr, "%s: %s: plugin must have a .open callback\n",
             program_name, filename);
    exit (EXIT_FAILURE);
  }
  if (p->plugin.get_size == NULL) {
    fprintf (stderr, "%s: %s: plugin must have a .get_size callback\n",
             program_name, filename);
    exit (EXIT_FAILURE);
  }
  if (p->plugin.pread == NULL && p->plugin._pread_v1 == NULL) {
    fprintf (stderr, "%s: %s: plugin must have a .pread callback\n",
             program_name, filename);
    exit (EXIT_FAILURE);
  }

  backend_load (&p->backend, p->plugin.name, p->plugin.load);

  return (struct backend *) p;
}
