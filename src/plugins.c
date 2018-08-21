/* nbdkit
 * Copyright (C) 2013-2018 Red Hat Inc.
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
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <errno.h>

#include <dlfcn.h>

#include "internal.h"

/* Maximum read or write request that we will handle. */
#define MAX_REQUEST_SIZE (64 * 1024 * 1024)

/* We extend the generic backend struct with extra fields relating
 * to this plugin.
 */
struct backend_plugin {
  struct backend backend;
  char *name;                   /* copy of plugin.name */
  char *filename;
  void *dl;
  struct nbdkit_plugin plugin;
};

static void
plugin_free (struct backend *b)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  /* Acquiring this lock prevents any plugin callbacks from running
   * simultaneously.
   */
  lock_unload ();

  debug ("%s: unload", p->name);
  if (p->plugin.unload)
    p->plugin.unload ();

  if (DO_DLCLOSE)
    dlclose (p->dl);
  free (p->filename);

  unlock_unload ();

  free (p->name);
  free (p);
}

static int
plugin_thread_model (struct backend *b)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  return p->plugin._thread_model;
}

static const char *
plugin_name (struct backend *b)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  return p->name;
}

static void
plugin_usage (struct backend *b)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  printf ("plugin: %s", p->name);
  if (p->plugin.longname)
    printf (" (%s)", p->plugin.longname);
  printf ("\n");
  printf ("(%s)", p->filename);
  if (p->plugin.description) {
    printf ("\n");
    printf ("%s\n", p->plugin.description);
  }
  if (p->plugin.config_help) {
    printf ("\n");
    printf ("%s\n", p->plugin.config_help);
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

  path = nbdkit_realpath (p->filename);
  printf ("path=%s\n", path);
  free (path);

  printf ("name=%s\n", p->name);
  if (p->plugin.version)
    printf ("version=%s\n", p->plugin.version);

  printf ("api_version=%d\n", p->plugin._api_version);
  printf ("struct_size=%" PRIu64 "\n", p->plugin._struct_size);
  printf ("thread_model=");
  switch (p->plugin._thread_model) {
  case NBDKIT_THREAD_MODEL_SERIALIZE_CONNECTIONS:
    printf ("serialize_connections");
    break;
  case NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS:
    printf ("serialize_all_requests");
    break;
  case NBDKIT_THREAD_MODEL_SERIALIZE_REQUESTS:
    printf ("serialize_requests");
    break;
  case NBDKIT_THREAD_MODEL_PARALLEL:
    printf ("parallel");
    break;
  default:
    printf ("%d # unknown thread model!", p->plugin._thread_model);
    break;
  }
  printf ("\n");
  printf ("errno_is_preserved=%d\n", p->plugin.errno_is_preserved);

#define HAS(field) if (p->plugin.field) printf ("has_%s=1\n", #field)
  HAS (longname);
  HAS (description);
  HAS (load);
  HAS (unload);
  HAS (dump_plugin);
  HAS (config);
  HAS (config_complete);
  HAS (config_help);
  HAS (open);
  HAS (close);
  HAS (get_size);
  HAS (can_write);
  HAS (can_flush);
  HAS (is_rotational);
  HAS (can_trim);
  HAS (_pread_old);
  HAS (_pwrite_old);
  HAS (_flush_old);
  HAS (_trim_old);
  HAS (_zero_old);
  HAS (can_fua);
  HAS (pread);
  HAS (pwrite);
  HAS (flush);
  HAS (trim);
  HAS (zero);
#undef HAS

  /* Custom fields. */
  if (p->plugin.dump_plugin)
    p->plugin.dump_plugin ();
}

static void
plugin_config (struct backend *b, const char *key, const char *value)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  debug ("%s: config key=%s, value=%s", p->name, key, value);

  if (p->plugin.config == NULL) {
    fprintf (stderr, "%s: %s: this plugin does not need command line configuration\n"
             "Try using: %s --help %s\n",
             program_name, p->filename,
             program_name, p->filename);
    exit (EXIT_FAILURE);
  }

  if (p->plugin.config (key, value) == -1)
    exit (EXIT_FAILURE);
}

static void
plugin_config_complete (struct backend *b)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  debug ("%s: config_complete", p->name);

  if (!p->plugin.config_complete)
    return;

  if (p->plugin.config_complete () == -1)
    exit (EXIT_FAILURE);
}

static int
plugin_open (struct backend *b, struct connection *conn, int readonly)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  void *handle;

  assert (connection_get_handle (conn, 0) == NULL);
  assert (p->plugin.open != NULL);

  debug ("%s: open readonly=%d", p->name, readonly);

  handle = p->plugin.open (readonly);
  if (!handle)
    return -1;

  connection_set_handle (conn, 0, handle);
  return 0;
}

/* We don't expose .prepare and .finalize to plugins since they aren't
 * necessary.  Plugins can easily do the same work in .open and
 * .close.
 */
static int
plugin_prepare (struct backend *b, struct connection *conn)
{
  return 0;
}

static int
plugin_finalize (struct backend *b, struct connection *conn)
{
  return 0;
}

static void
plugin_close (struct backend *b, struct connection *conn)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  assert (connection_get_handle (conn, 0));

  debug ("close");

  if (p->plugin.close)
    p->plugin.close (connection_get_handle (conn, 0));

  connection_set_handle (conn, 0, NULL);
}

static int64_t
plugin_get_size (struct backend *b, struct connection *conn)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  assert (connection_get_handle (conn, 0));
  assert (p->plugin.get_size != NULL);

  debug ("get_size");

  return p->plugin.get_size (connection_get_handle (conn, 0));
}

static int
plugin_can_write (struct backend *b, struct connection *conn)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  assert (connection_get_handle (conn, 0));

  debug ("can_write");

  if (p->plugin.can_write)
    return p->plugin.can_write (connection_get_handle (conn, 0));
  else
    return p->plugin.pwrite || p->plugin._pwrite_old;
}

static int
plugin_can_flush (struct backend *b, struct connection *conn)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  assert (connection_get_handle (conn, 0));

  debug ("can_flush");

  if (p->plugin.can_flush)
    return p->plugin.can_flush (connection_get_handle (conn, 0));
  else
    return p->plugin.flush || p->plugin._flush_old;
}

static int
plugin_is_rotational (struct backend *b, struct connection *conn)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  assert (connection_get_handle (conn, 0));

  debug ("is_rotational");

  if (p->plugin.is_rotational)
    return p->plugin.is_rotational (connection_get_handle (conn, 0));
  else
    return 0; /* assume false */
}

static int
plugin_can_trim (struct backend *b, struct connection *conn)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  assert (connection_get_handle (conn, 0));

  debug ("can_trim");

  if (p->plugin.can_trim)
    return p->plugin.can_trim (connection_get_handle (conn, 0));
  else
    return p->plugin.trim || p->plugin._trim_old;
}

static int
plugin_can_zero (struct backend *b, struct connection *conn)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);

  assert (connection_get_handle (conn, 0));

  debug ("can_zero");

  /* Note the special case here: the plugin's .can_zero controls only
   * whether we call .zero; while the backend expects .can_zero to
   * return whether to advertise zero support.  Since we ALWAYS know
   * how to fall back to .pwrite in plugin_zero(), we ignore the
   * difference between the plugin's true or false return, and only
   * call it to catch a -1 failure during negotiation.  */
  if (p->plugin.can_zero &&
      p->plugin.can_zero (connection_get_handle (conn, 0)) == -1)
    return -1;
  return plugin_can_write (b, conn);
}

static int
plugin_can_fua (struct backend *b, struct connection *conn)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  int r;

  assert (connection_get_handle (conn, 0));

  debug ("can_fua");

  /* The plugin must use API version 2 and have .can_fua return
     NBDKIT_FUA_NATIVE before we will pass the FUA flag on. */
  if (p->plugin.can_fua) {
    r = p->plugin.can_fua (connection_get_handle (conn, 0));
    if (r > NBDKIT_FUA_EMULATE && p->plugin._api_version == 1)
      r = NBDKIT_FUA_EMULATE;
    return r;
  }
  /* We intend to call .flush even if .can_flush returns false. */
  if (p->plugin.flush || p->plugin._flush_old)
    return NBDKIT_FUA_EMULATE;
  return NBDKIT_FUA_NONE;
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

  if (!ret && p->plugin.errno_is_preserved)
    ret = errno;
  return ret ? ret : EIO;
}

static int
plugin_pread (struct backend *b, struct connection *conn,
              void *buf, uint32_t count, uint64_t offset, uint32_t flags,
              int *err)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  int r;

  assert (connection_get_handle (conn, 0));
  assert (p->plugin.pread || p->plugin._pread_old);
  assert (!flags);

  debug ("pread count=%" PRIu32 " offset=%" PRIu64, count, offset);

  if (p->plugin.pread)
    r = p->plugin.pread (connection_get_handle (conn, 0), buf, count, offset,
                         0);
  else
    r = p->plugin._pread_old (connection_get_handle (conn, 0), buf, count,
                              offset);
  if (r == -1)
    *err = get_error (p);
  return r;
}

static int
plugin_flush (struct backend *b, struct connection *conn, uint32_t flags,
              int *err)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  int r;

  assert (connection_get_handle (conn, 0));
  assert (!flags);

  debug ("flush");

  if (p->plugin.flush)
    r = p->plugin.flush (connection_get_handle (conn, 0), 0);
  else if (p->plugin._flush_old)
    r = p->plugin._flush_old (connection_get_handle (conn, 0));
  else {
    *err = EINVAL;
    return -1;
  }
  if (r == -1)
    *err = get_error (p);
  return r;
}

static int
plugin_pwrite (struct backend *b, struct connection *conn,
               const void *buf, uint32_t count, uint64_t offset, uint32_t flags,
               int *err)
{
  int r;
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  bool fua = flags & NBDKIT_FLAG_FUA;
  bool need_flush = false;

  assert (connection_get_handle (conn, 0));
  assert (!(flags & ~NBDKIT_FLAG_FUA));

  debug ("pwrite count=%" PRIu32 " offset=%" PRIu64 " fua=%d", count, offset,
         fua);

  if (fua && plugin_can_fua (b, conn) != NBDKIT_FUA_NATIVE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }
  if (p->plugin.pwrite)
    r = p->plugin.pwrite (connection_get_handle (conn, 0), buf, count, offset,
                          flags);
  else if (p->plugin._pwrite_old)
    r = p->plugin._pwrite_old (connection_get_handle (conn, 0),
                               buf, count, offset);
  else {
    *err = EROFS;
    return -1;
  }
  if (r != -1 && need_flush)
    r = plugin_flush (b, conn, 0, err);
  if (r == -1)
    *err = get_error (p);
  return r;
}

static int
plugin_trim (struct backend *b, struct connection *conn,
             uint32_t count, uint64_t offset, uint32_t flags, int *err)
{
  int r;
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  bool fua = flags & NBDKIT_FLAG_FUA;
  bool need_flush = false;

  assert (connection_get_handle (conn, 0));
  assert (!(flags & ~NBDKIT_FLAG_FUA));

  debug ("trim count=%" PRIu32 " offset=%" PRIu64 " fua=%d", count, offset,
         fua);

  if (fua && plugin_can_fua (b, conn) != NBDKIT_FUA_NATIVE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }
  if (p->plugin.trim)
    r = p->plugin.trim (connection_get_handle (conn, 0), count, offset, flags);
  else if (p->plugin._trim_old)
    r = p->plugin._trim_old (connection_get_handle (conn, 0), count, offset);
  else {
    *err = EINVAL;
    return -1;
  }
  if (r != -1 && need_flush)
    r = plugin_flush (b, conn, 0, err);
  if (r == -1)
    *err = get_error (p);
  return r;
}

static int
plugin_zero (struct backend *b, struct connection *conn,
             uint32_t count, uint64_t offset, uint32_t flags, int *err)
{
  struct backend_plugin *p = container_of (b, struct backend_plugin, backend);
  char *buf;
  uint32_t limit;
  int r = -1;
  bool may_trim = flags & NBDKIT_FLAG_MAY_TRIM;
  bool fua = flags & NBDKIT_FLAG_FUA;
  bool emulate = false;
  bool need_flush = false;
  int can_zero = 1; /* TODO cache this per-connection? */

  assert (connection_get_handle (conn, 0));
  assert (!(flags & ~(NBDKIT_FLAG_MAY_TRIM | NBDKIT_FLAG_FUA)));

  debug ("zero count=%" PRIu32 " offset=%" PRIu64 " may_trim=%d fua=%d",
         count, offset, may_trim, fua);

  if (fua && plugin_can_fua (b, conn) != NBDKIT_FUA_NATIVE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }
  if (!count)
    return 0;
  if (p->plugin.can_zero) {
    can_zero = p->plugin.can_zero (connection_get_handle (conn, 0));
    assert (can_zero != -1);
  }

  if (can_zero) {
    errno = 0;
    if (p->plugin.zero)
      r = p->plugin.zero (connection_get_handle (conn, 0), count, offset,
                          flags);
    else if (p->plugin._zero_old)
      r = p->plugin._zero_old (connection_get_handle (conn, 0), count, offset,
                               may_trim);
    else
      emulate = true;
    if (r == -1)
      *err = emulate ? EOPNOTSUPP : get_error (p);
    if (r == 0 || *err != EOPNOTSUPP)
      goto done;
  }

  assert (p->plugin.pwrite || p->plugin._pwrite_old);
  flags &= ~NBDKIT_FLAG_MAY_TRIM;
  threadlocal_set_error (0);
  limit = count < MAX_REQUEST_SIZE ? count : MAX_REQUEST_SIZE;
  buf = calloc (limit, 1);
  if (!buf) {
    *err = ENOMEM;
    return -1;
  }

  while (count) {
    r = plugin_pwrite (b, conn, buf, limit, offset, flags, err);
    if (r == -1)
      break;
    count -= limit;
    if (count < limit)
      limit = count;
  }

  *err = errno;
  free (buf);
  errno = *err;

 done:
  if (r != -1 && need_flush)
    r = plugin_flush (b, conn, 0, err);
  if (r == -1)
    *err = get_error (p);
  return r;
}

static struct backend plugin_functions = {
  .free = plugin_free,
  .thread_model = plugin_thread_model,
  .name = plugin_name,
  .plugin_name = plugin_name,
  .usage = plugin_usage,
  .version = plugin_version,
  .dump_fields = plugin_dump_fields,
  .config = plugin_config,
  .config_complete = plugin_config_complete,
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
  .can_fua = plugin_can_fua,
  .pread = plugin_pread,
  .pwrite = plugin_pwrite,
  .flush = plugin_flush,
  .trim = plugin_trim,
  .zero = plugin_zero,
};

/* Register and load a plugin. */
struct backend *
plugin_register (size_t index, const char *filename,
                 void *dl, struct nbdkit_plugin *(*plugin_init) (void))
{
  struct backend_plugin *p;
  const struct nbdkit_plugin *plugin;
  size_t i, len, size;

  p = malloc (sizeof *p);
  if (p == NULL) {
  out_of_memory:
    perror ("strdup");
    exit (EXIT_FAILURE);
  }

  p->backend = plugin_functions;
  p->backend.next = NULL;
  p->backend.i = index;
  p->filename = strdup (filename);
  if (p->filename == NULL) goto out_of_memory;
  p->dl = dl;

  debug ("registering plugin %s", p->filename);

  /* Call the initialization function which returns the address of the
   * plugin's own 'struct nbdkit_plugin'.
   */
  plugin = plugin_init ();
  if (!plugin) {
    fprintf (stderr, "%s: %s: plugin registration function failed\n",
             program_name, p->filename);
    exit (EXIT_FAILURE);
  }

  /* Check for incompatible future versions. */
  if (plugin->_api_version < 0 || plugin->_api_version > 2) {
    fprintf (stderr, "%s: %s: plugin is incompatible with this version of nbdkit (_api_version = %d)\n",
             program_name, p->filename, plugin->_api_version);
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
  if (p->plugin.name == NULL) {
    fprintf (stderr, "%s: %s: plugin must have a .name field\n",
             program_name, p->filename);
    exit (EXIT_FAILURE);
  }
  if (p->plugin.open == NULL) {
    fprintf (stderr, "%s: %s: plugin must have a .open callback\n",
             program_name, p->filename);
    exit (EXIT_FAILURE);
  }
  if (p->plugin.get_size == NULL) {
    fprintf (stderr, "%s: %s: plugin must have a .get_size callback\n",
             program_name, p->filename);
    exit (EXIT_FAILURE);
  }
  if (p->plugin.pread == NULL && p->plugin._pread_old == NULL) {
    fprintf (stderr, "%s: %s: plugin must have a .pread callback\n",
             program_name, p->filename);
    exit (EXIT_FAILURE);
  }

  len = strlen (p->plugin.name);
  if (len == 0) {
    fprintf (stderr, "%s: %s: plugin.name field must not be empty\n",
             program_name, p->filename);
    exit (EXIT_FAILURE);
  }
  for (i = 0; i < len; ++i) {
    if (!((p->plugin.name[i] >= '0' && p->plugin.name[i] <= '9') ||
          (p->plugin.name[i] >= 'a' && p->plugin.name[i] <= 'z') ||
          (p->plugin.name[i] >= 'A' && p->plugin.name[i] <= 'Z'))) {
      fprintf (stderr, "%s: %s: plugin.name ('%s') field must contain only ASCII alphanumeric characters\n",
               program_name, p->filename, p->plugin.name);
      exit (EXIT_FAILURE);
    }
  }

  /* Copy the module's name into local storage, so that plugin.name
   * survives past unload.
   */
  p->name = strdup (p->plugin.name);
  if (p->name == NULL) {
    perror ("strdup");
    exit (EXIT_FAILURE);
  }

  debug ("registered plugin %s (name %s)", p->filename, p->name);

  /* Set debug flags before calling load. */
  set_debug_flags (dl, p->name);

  /* Call the on-load callback if it exists. */
  debug ("%s: load", p->name);
  if (p->plugin.load)
    p->plugin.load ();

  return (struct backend *) p;
}

/* Set all debug flags which apply to this plugin (also used by filters). */
void
set_debug_flags (void *dl, const char *name)
{
  struct debug_flag *flag;

  for (flag = debug_flags; flag != NULL; flag = flag->next) {
    if (!flag->used && strcmp (name, flag->name) == 0) {
      CLEANUP_FREE char *var = NULL;
      int *sym;

      /* Synthesize the name of the variable. */
      if (asprintf (&var, "%s_debug_%s", name, flag->flag) == -1) {
        perror ("asprintf");
        exit (EXIT_FAILURE);
      }

      /* Find the symbol. */
      sym = dlsym (dl, var);
      if (sym == NULL) {
        fprintf (stderr,
                 "%s: -D %s.%s: %s does not contain a "
                 "global variable called %s\n",
                 program_name, name, flag->flag, name, var);
        exit (EXIT_FAILURE);
      }

      /* Set the flag. */
      *sym = flag->value;

      /* Mark this flag as used. */
      flag->used = 1;
    }
  }
}
