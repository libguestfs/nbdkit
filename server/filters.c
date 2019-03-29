/* nbdkit
 * Copyright (C) 2013-2018 Red Hat Inc.
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

#include <dlfcn.h>

#include "internal.h"

/* We extend the generic backend struct with extra fields relating
 * to this filter.
 */
struct backend_filter {
  struct backend backend;
  char *name;                   /* copy of filter.name */
  char *filename;
  void *dl;
  struct nbdkit_filter filter;
};

/* Literally a backend + a connection pointer.  This is the
 * implementation of ‘void *nxdata’ in the filter API.
 */
struct b_conn {
  struct backend *b;
  struct connection *conn;
};

/* Note this frees the whole chain. */
static void
filter_free (struct backend *b)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);

  f->backend.next->free (f->backend.next);

  /* Acquiring this lock prevents any filter callbacks from running
   * simultaneously.
   */
  lock_unload ();

  debug ("%s: unload", f->name);
  if (f->filter.unload)
    f->filter.unload ();

  if (DO_DLCLOSE)
    dlclose (f->dl);
  free (f->filename);

  unlock_unload ();

  free (f->name);
  free (f);
}

static int
filter_thread_model (struct backend *b)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  int filter_thread_model = f->filter._thread_model;
  int thread_model = f->backend.next->thread_model (f->backend.next);

  if (filter_thread_model < thread_model) /* more serialized */
    thread_model = filter_thread_model;

  return thread_model;
}

/* This is actually passing the request through to the final plugin,
 * hence the function name.
 */
static const char *
plugin_name (struct backend *b)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);

  return f->backend.next->plugin_name (f->backend.next);
}

static const char *
filter_name (struct backend *b)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);

  return f->name;
}

static const char *
filter_version (struct backend *b)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);

  return f->filter.version;
}

static void
filter_usage (struct backend *b)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  const char *p;

  printf ("filter: %s", f->name);
  if (f->filter.longname)
    printf (" (%s)", f->filter.longname);
  printf ("\n");
  printf ("(%s)\n", f->filename);
  if (f->filter.description) {
    printf ("%s", f->filter.description);
    if ((p = strrchr (f->filter.description, '\n')) == NULL || p[1])
      printf ("\n");
  }
  if (f->filter.config_help) {
    printf ("%s", f->filter.config_help);
    if ((p = strrchr (f->filter.config_help, '\n')) == NULL || p[1])
      printf ("\n");
  }
}

static void
filter_dump_fields (struct backend *b)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);

  f->backend.next->dump_fields (f->backend.next);
}

static int
next_config (void *nxdata, const char *key, const char *value)
{
  struct backend *b = nxdata;
  b->config (b, key, value);
  return 0;
}

static void
filter_config (struct backend *b, const char *key, const char *value)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);

  debug ("%s: config key=%s, value=%s",
         f->name, key, value);

  if (f->filter.config) {
    if (f->filter.config (next_config, f->backend.next, key, value) == -1)
      exit (EXIT_FAILURE);
  }
  else
    f->backend.next->config (f->backend.next, key, value);
}

static int
next_config_complete (void *nxdata)
{
  struct backend *b = nxdata;
  b->config_complete (b);
  return 0;
}

static void
filter_config_complete (struct backend *b)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);

  debug ("%s: config_complete", f->name);

  if (f->filter.config_complete) {
    if (f->filter.config_complete (next_config_complete, f->backend.next) == -1)
      exit (EXIT_FAILURE);
  }
  else
    f->backend.next->config_complete (f->backend.next);
}

/* magic_config_key only applies to plugins, so this passes the
 * request through to the plugin (hence the name).
 */
static const char *
plugin_magic_config_key (struct backend *b)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);

  return f->backend.next->magic_config_key (f->backend.next);
}

static int
next_open (void *nxdata, int readonly)
{
  struct b_conn *b_conn = nxdata;

  return b_conn->b->open (b_conn->b, b_conn->conn, readonly);
}

static int
filter_open (struct backend *b, struct connection *conn, int readonly)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };
  void *handle;

  debug ("%s: open readonly=%d", f->name, readonly);

  if (f->filter.open) {
    handle = f->filter.open (next_open, &nxdata, readonly);
    if (handle == NULL)
      return -1;
    connection_set_handle (conn, f->backend.i, handle);
    return 0;
  }
  else
    return f->backend.next->open (f->backend.next, conn, readonly);
}

static void
filter_close (struct backend *b, struct connection *conn)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);

  debug ("%s: close", f->name);

  if (f->filter.close)
    f->filter.close (handle);
  f->backend.next->close (f->backend.next, conn);
}

/* The next_functions structure contains pointers to backend
 * functions.  However because these functions are all expecting a
 * backend and a connection, we cannot call them directly, but must
 * write some next_* functions that unpack the two parameters from a
 * single ‘void *nxdata’ struct pointer (‘b_conn’).
 */

static int64_t
next_get_size (void *nxdata)
{
  struct b_conn *b_conn = nxdata;
  return b_conn->b->get_size (b_conn->b, b_conn->conn);
}

static int
next_can_write (void *nxdata)
{
  struct b_conn *b_conn = nxdata;
  return b_conn->b->can_write (b_conn->b, b_conn->conn);
}

static int
next_can_flush (void *nxdata)
{
  struct b_conn *b_conn = nxdata;
  return b_conn->b->can_flush (b_conn->b, b_conn->conn);
}

static int
next_is_rotational (void *nxdata)
{
  struct b_conn *b_conn = nxdata;
  return b_conn->b->is_rotational (b_conn->b, b_conn->conn);
}

static int
next_can_trim (void *nxdata)
{
  struct b_conn *b_conn = nxdata;
  return b_conn->b->can_trim (b_conn->b, b_conn->conn);
}

static int
next_can_zero (void *nxdata)
{
  struct b_conn *b_conn = nxdata;
  return b_conn->b->can_zero (b_conn->b, b_conn->conn);
}

static int
next_can_extents (void *nxdata)
{
  struct b_conn *b_conn = nxdata;
  return b_conn->b->can_extents (b_conn->b, b_conn->conn);
}

static int
next_can_fua (void *nxdata)
{
  struct b_conn *b_conn = nxdata;
  return b_conn->b->can_fua (b_conn->b, b_conn->conn);
}

static int
next_can_multi_conn (void *nxdata)
{
  struct b_conn *b_conn = nxdata;
  return b_conn->b->can_multi_conn (b_conn->b, b_conn->conn);
}

static int
next_pread (void *nxdata, void *buf, uint32_t count, uint64_t offset,
            uint32_t flags, int *err)
{
  struct b_conn *b_conn = nxdata;
  return b_conn->b->pread (b_conn->b, b_conn->conn, buf, count, offset, flags,
                           err);
}

static int
next_pwrite (void *nxdata, const void *buf, uint32_t count, uint64_t offset,
             uint32_t flags, int *err)
{
  struct b_conn *b_conn = nxdata;
  return b_conn->b->pwrite (b_conn->b, b_conn->conn, buf, count, offset, flags,
                            err);
}

static int
next_flush (void *nxdata, uint32_t flags, int *err)
{
  struct b_conn *b_conn = nxdata;
  return b_conn->b->flush (b_conn->b, b_conn->conn, flags, err);
}

static int
next_trim (void *nxdata, uint32_t count, uint64_t offset, uint32_t flags,
           int *err)
{
  struct b_conn *b_conn = nxdata;
  return b_conn->b->trim (b_conn->b, b_conn->conn, count, offset, flags, err);
}

static int
next_zero (void *nxdata, uint32_t count, uint64_t offset, uint32_t flags,
           int *err)
{
  struct b_conn *b_conn = nxdata;
  return b_conn->b->zero (b_conn->b, b_conn->conn, count, offset, flags, err);
}

static int
next_extents (void *nxdata, uint32_t count, uint64_t offset, uint32_t flags,
              struct nbdkit_extents *extents, int *err)
{
  struct b_conn *b_conn = nxdata;
  return b_conn->b->extents (b_conn->b, b_conn->conn, count, offset, flags,
                             extents, err);
}

static struct nbdkit_next_ops next_ops = {
  .get_size = next_get_size,
  .can_write = next_can_write,
  .can_flush = next_can_flush,
  .is_rotational = next_is_rotational,
  .can_trim = next_can_trim,
  .can_zero = next_can_zero,
  .can_extents = next_can_extents,
  .can_fua = next_can_fua,
  .can_multi_conn = next_can_multi_conn,
  .pread = next_pread,
  .pwrite = next_pwrite,
  .flush = next_flush,
  .trim = next_trim,
  .zero = next_zero,
  .extents = next_extents,
};

static int
filter_prepare (struct backend *b, struct connection *conn)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };

  debug ("%s: prepare", f->name);

  /* Call these in order starting from the filter closest to the
   * plugin.
   */
  if (f->backend.next->prepare (f->backend.next, conn) == -1)
    return -1;

  if (f->filter.prepare &&
      f->filter.prepare (&next_ops, &nxdata, handle) == -1)
    return -1;

  return 0;
}

static int
filter_finalize (struct backend *b, struct connection *conn)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };

  debug ("%s: finalize", f->name);

  /* Call these in reverse order to .prepare above, starting from the
   * filter furthest away from the plugin.
   */
  if (f->filter.finalize &&
      f->filter.finalize (&next_ops, &nxdata, handle) == -1)
    return -1;

  return f->backend.next->finalize (f->backend.next, conn);
}

static int64_t
filter_get_size (struct backend *b, struct connection *conn)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };

  debug ("%s: get_size", f->name);

  if (f->filter.get_size)
    return f->filter.get_size (&next_ops, &nxdata, handle);
  else
    return f->backend.next->get_size (f->backend.next, conn);
}

static int
filter_can_write (struct backend *b, struct connection *conn)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };

  debug ("%s: can_write", f->name);

  if (f->filter.can_write)
    return f->filter.can_write (&next_ops, &nxdata, handle);
  else
    return f->backend.next->can_write (f->backend.next, conn);
}

static int
filter_can_flush (struct backend *b, struct connection *conn)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };

  debug ("%s: can_flush", f->name);

  if (f->filter.can_flush)
    return f->filter.can_flush (&next_ops, &nxdata, handle);
  else
    return f->backend.next->can_flush (f->backend.next, conn);
}

static int
filter_is_rotational (struct backend *b, struct connection *conn)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };

  debug ("%s: is_rotational", f->name);

  if (f->filter.is_rotational)
    return f->filter.is_rotational (&next_ops, &nxdata, handle);
  else
    return f->backend.next->is_rotational (f->backend.next, conn);
}

static int
filter_can_trim (struct backend *b, struct connection *conn)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };

  debug ("%s: can_trim", f->name);

  if (f->filter.can_trim)
    return f->filter.can_trim (&next_ops, &nxdata, handle);
  else
    return f->backend.next->can_trim (f->backend.next, conn);
}

static int
filter_can_zero (struct backend *b, struct connection *conn)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };

  debug ("%s: can_zero", f->name);

  if (f->filter.can_zero)
    return f->filter.can_zero (&next_ops, &nxdata, handle);
  else
    return f->backend.next->can_zero (f->backend.next, conn);
}

static int
filter_can_extents (struct backend *b, struct connection *conn)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };

  debug ("%s: can_extents", f->name);

  if (f->filter.can_extents)
    return f->filter.can_extents (&next_ops, &nxdata, handle);
  else
    return f->backend.next->can_extents (f->backend.next, conn);
}

static int
filter_can_fua (struct backend *b, struct connection *conn)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };

  debug ("%s: can_fua", f->name);

  if (f->filter.can_fua)
    return f->filter.can_fua (&next_ops, &nxdata, handle);
  else
    return f->backend.next->can_fua (f->backend.next, conn);
}

static int
filter_can_multi_conn (struct backend *b, struct connection *conn)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };

  debug ("%s: can_multi_conn", f->name);

  if (f->filter.can_multi_conn)
    return f->filter.can_multi_conn (&next_ops, &nxdata, handle);
  else
    return f->backend.next->can_multi_conn (f->backend.next, conn);
}

static int
filter_pread (struct backend *b, struct connection *conn,
              void *buf, uint32_t count, uint64_t offset,
              uint32_t flags, int *err)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };

  assert (flags == 0);

  debug ("%s: pread count=%" PRIu32 " offset=%" PRIu64 " flags=0x%" PRIx32,
         f->name, count, offset, flags);

  if (f->filter.pread)
    return f->filter.pread (&next_ops, &nxdata, handle,
                            buf, count, offset, flags, err);
  else
    return f->backend.next->pread (f->backend.next, conn,
                                   buf, count, offset, flags, err);
}

static int
filter_pwrite (struct backend *b, struct connection *conn,
               const void *buf, uint32_t count, uint64_t offset,
               uint32_t flags, int *err)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };

  assert (!(flags & ~NBDKIT_FLAG_FUA));

  debug ("%s: pwrite count=%" PRIu32 " offset=%" PRIu64 " flags=0x%" PRIx32,
         f->name, count, offset, flags);

  if (f->filter.pwrite)
    return f->filter.pwrite (&next_ops, &nxdata, handle,
                             buf, count, offset, flags, err);
  else
    return f->backend.next->pwrite (f->backend.next, conn,
                                    buf, count, offset, flags, err);
}

static int
filter_flush (struct backend *b, struct connection *conn, uint32_t flags,
              int *err)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };

  assert (flags == 0);

  debug ("%s: flush flags=0x%" PRIx32, f->name, flags);

  if (f->filter.flush)
    return f->filter.flush (&next_ops, &nxdata, handle, flags, err);
  else
    return f->backend.next->flush (f->backend.next, conn, flags, err);
}

static int
filter_trim (struct backend *b, struct connection *conn,
             uint32_t count, uint64_t offset,
             uint32_t flags, int *err)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };

  assert (flags == 0);

  debug ("%s: trim count=%" PRIu32 " offset=%" PRIu64 " flags=0x%" PRIx32,
         f->name, count, offset, flags);

  if (f->filter.trim)
    return f->filter.trim (&next_ops, &nxdata, handle, count, offset, flags,
                           err);
  else
    return f->backend.next->trim (f->backend.next, conn, count, offset, flags,
                                  err);
}

static int
filter_zero (struct backend *b, struct connection *conn,
             uint32_t count, uint64_t offset, uint32_t flags, int *err)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };

  assert (!(flags & ~(NBDKIT_FLAG_MAY_TRIM | NBDKIT_FLAG_FUA)));

  debug ("%s: zero count=%" PRIu32 " offset=%" PRIu64 " flags=0x%" PRIx32,
         f->name, count, offset, flags);

  if (f->filter.zero)
    return f->filter.zero (&next_ops, &nxdata, handle,
                           count, offset, flags, err);
  else
    return f->backend.next->zero (f->backend.next, conn,
                                  count, offset, flags, err);
}

static int
filter_extents (struct backend *b, struct connection *conn,
                uint32_t count, uint64_t offset, uint32_t flags,
                struct nbdkit_extents *extents, int *err)
{
  struct backend_filter *f = container_of (b, struct backend_filter, backend);
  void *handle = connection_get_handle (conn, f->backend.i);
  struct b_conn nxdata = { .b = f->backend.next, .conn = conn };

  assert (!(flags & ~NBDKIT_FLAG_REQ_ONE));

  debug ("%s: extents count=%" PRIu32 " offset=%" PRIu64 " flags=0x%" PRIx32,
         f->name, count, offset, flags);

  if (f->filter.extents)
    return f->filter.extents (&next_ops, &nxdata, handle,
                              count, offset, flags,
                              extents, err);
  else
    return f->backend.next->extents (f->backend.next, conn,
                                     count, offset, flags,
                                     extents, err);
}

static struct backend filter_functions = {
  .free = filter_free,
  .thread_model = filter_thread_model,
  .name = filter_name,
  .plugin_name = plugin_name,
  .usage = filter_usage,
  .version = filter_version,
  .dump_fields = filter_dump_fields,
  .config = filter_config,
  .config_complete = filter_config_complete,
  .magic_config_key = plugin_magic_config_key,
  .open = filter_open,
  .prepare = filter_prepare,
  .finalize = filter_finalize,
  .close = filter_close,
  .get_size = filter_get_size,
  .can_write = filter_can_write,
  .can_flush = filter_can_flush,
  .is_rotational = filter_is_rotational,
  .can_trim = filter_can_trim,
  .can_zero = filter_can_zero,
  .can_extents = filter_can_extents,
  .can_fua = filter_can_fua,
  .can_multi_conn = filter_can_multi_conn,
  .pread = filter_pread,
  .pwrite = filter_pwrite,
  .flush = filter_flush,
  .trim = filter_trim,
  .zero = filter_zero,
  .extents = filter_extents,
};

/* Register and load a filter. */
struct backend *
filter_register (struct backend *next, size_t index, const char *filename,
                 void *dl, struct nbdkit_filter *(*filter_init) (void))
{
  struct backend_filter *f;
  const struct nbdkit_filter *filter;
  size_t i, len;

  f = calloc (1, sizeof *f);
  if (f == NULL) {
  out_of_memory:
    perror ("strdup");
    exit (EXIT_FAILURE);
  }

  f->backend = filter_functions;
  f->backend.next = next;
  f->backend.i = index;
  f->filename = strdup (filename);
  if (f->filename == NULL) goto out_of_memory;
  f->dl = dl;

  debug ("registering filter %s", f->filename);

  /* Call the initialization function which returns the address of the
   * filter's own 'struct nbdkit_filter'.
   */
  filter = filter_init ();
  if (!filter) {
    fprintf (stderr, "%s: %s: filter registration function failed\n",
             program_name, f->filename);
    exit (EXIT_FAILURE);
  }

  /* We do not provide API or ABI guarantees for filters, other than
   * the ABI position of _api_version that will let us diagnose
   * mismatch when the API changes.
   */
  if (filter->_api_version != NBDKIT_FILTER_API_VERSION) {
    fprintf (stderr,
             "%s: %s: filter is incompatible with this version of nbdkit "
             "(_api_version = %d)\n",
             program_name, f->filename, filter->_api_version);
    exit (EXIT_FAILURE);
  }

  f->filter = *filter;

  /* Only filter.name is required. */
  if (f->filter.name == NULL) {
    fprintf (stderr, "%s: %s: filter must have a .name field\n",
             program_name, f->filename);
    exit (EXIT_FAILURE);
  }

  len = strlen (f->filter.name);
  if (len == 0) {
    fprintf (stderr, "%s: %s: filter.name field must not be empty\n",
             program_name, f->filename);
    exit (EXIT_FAILURE);
  }
  for (i = 0; i < len; ++i) {
    if (!((f->filter.name[i] >= '0' && f->filter.name[i] <= '9') ||
          (f->filter.name[i] >= 'a' && f->filter.name[i] <= 'z') ||
          (f->filter.name[i] >= 'A' && f->filter.name[i] <= 'Z'))) {
      fprintf (stderr,
               "%s: %s: filter.name ('%s') field "
               "must contain only ASCII alphanumeric characters\n",
               program_name, f->filename, f->filter.name);
      exit (EXIT_FAILURE);
    }
  }

  /* Copy the module's name into local storage, so that filter.name
   * survives past unload.
   */
  f->name = strdup (f->filter.name);
  if (f->name == NULL) {
    perror ("strdup");
    exit (EXIT_FAILURE);
  }

  debug ("registered filter %s (name %s)", f->filename, f->name);

  /* Set debug flags before calling load. */
  set_debug_flags (dl, f->name);

  /* Call the on-load callback if it exists. */
  debug ("%s: load", f->name);
  if (f->filter.load)
    f->filter.load ();

  return (struct backend *) f;
}
