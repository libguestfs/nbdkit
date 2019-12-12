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
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include <dlfcn.h>

#include "internal.h"
#include "minmax.h"

/* Helpers for registering a new backend. */

void
backend_init (struct backend *b, struct backend *next, size_t index,
              const char *filename, void *dl, const char *type)
{
  b->next = next;
  b->i = index;
  b->type = type;
  b->filename = strdup (filename);
  if (b->filename == NULL) {
    perror ("strdup");
    exit (EXIT_FAILURE);
  }
  b->dl = dl;

  debug ("registering %s %s", type, filename);
}

void
backend_load (struct backend *b, const char *name, void (*load) (void))
{
  size_t i, len;

  /* name is required. */
  if (name == NULL) {
    fprintf (stderr, "%s: %s: %s must have a .name field\n",
             program_name, b->filename, b->type);
    exit (EXIT_FAILURE);
  }

  len = strlen (name);
  if (len == 0) {
    fprintf (stderr, "%s: %s: %s.name field must not be empty\n",
             program_name, b->filename, b->type);
    exit (EXIT_FAILURE);
  }
  for (i = 0; i < len; ++i) {
    unsigned char c = name[i];

    if (!(isascii (c) && isalnum (c))) {
      fprintf (stderr,
               "%s: %s: %s.name ('%s') field "
               "must contain only ASCII alphanumeric characters\n",
               program_name, b->filename, b->type, name);
      exit (EXIT_FAILURE);
    }
  }

  /* Copy the module's name into local storage, so that name
   * survives past unload.
   */
  b->name = strdup (name);
  if (b->name == NULL) {
    perror ("strdup");
    exit (EXIT_FAILURE);
  }

  debug ("registered %s %s (name %s)", b->type, b->filename, b->name);

  /* Apply debug flags before calling load. */
  apply_debug_flags (b->dl, name);

  /* Call the on-load callback if it exists. */
  debug ("%s: load", name);
  if (load)
    load ();
}

void
backend_unload (struct backend *b, void (*unload) (void))
{
  /* Acquiring this lock prevents any other backend callbacks from running
   * simultaneously.
   */
  lock_unload ();

  debug ("%s: unload %s", b->name, b->type);
  if (unload)
    unload ();

  if (DO_DLCLOSE)
    dlclose (b->dl);
  free (b->filename);

  unlock_unload ();

  free (b->name);
}

int
backend_open (struct backend *b, struct connection *conn, int readonly)
{
  struct b_conn_handle *h = &conn->handles[b->i];

  debug ("%s: open readonly=%d", b->name, readonly);

  assert (h->handle == NULL);
  assert ((h->state & HANDLE_OPEN) == 0);
  assert (h->can_write == -1);
  if (readonly)
    h->can_write = 0;

  /* Most filters will call next_open first, resulting in
   * inner-to-outer ordering.
   */
  h->handle = b->open (b, conn, readonly);
  debug ("%s: open returned handle %p", b->name, h->handle);

  if (h->handle == NULL) {
    if (b->i) /* Do not strand backend if this layer failed */
      backend_close (b->next, conn);
    return -1;
  }

  h->state |= HANDLE_OPEN;
  if (b->i) /* A filter must not succeed unless its backend did also */
    assert (conn->handles[b->i - 1].handle);
  return 0;
}

int
backend_prepare (struct backend *b, struct connection *conn)
{
  struct b_conn_handle *h = &conn->handles[b->i];

  assert (h->handle);
  assert ((h->state & (HANDLE_OPEN | HANDLE_CONNECTED)) == HANDLE_OPEN);

  /* Call these in order starting from the filter closest to the
   * plugin, similar to typical .open order.
   */
  if (b->i && backend_prepare (b->next, conn) == -1)
    return -1;

  debug ("%s: prepare readonly=%d", b->name, h->can_write == 0);

  if (b->prepare (b, conn, h->handle, h->can_write == 0) == -1)
    return -1;
  h->state |= HANDLE_CONNECTED;
  return 0;
}

int
backend_finalize (struct backend *b, struct connection *conn)
{
  struct b_conn_handle *h = &conn->handles[b->i];

  /* Call these in reverse order to .prepare above, starting from the
   * filter furthest away from the plugin, and matching .close order.
   */

  debug ("%s: finalize", b->name);

  /* Once finalize fails, we can do nothing further on this connection */
  if (h->state & HANDLE_FAILED)
    return -1;

  if (h->handle) {
    assert (h->state & HANDLE_CONNECTED);
    if (b->finalize (b, conn, h->handle) == -1) {
      h->state |= HANDLE_FAILED;
      return -1;
    }
  }
  else
    assert (! (h->state & HANDLE_CONNECTED));

  if (b->i)
    return backend_finalize (b->next, conn);
  return 0;
}

void
backend_close (struct backend *b, struct connection *conn)
{
  struct b_conn_handle *h = &conn->handles[b->i];

  /* outer-to-inner order, opposite .open */
  debug ("%s: close", b->name);

  if (h->handle) {
    assert (h->state & HANDLE_OPEN);
    b->close (b, conn, h->handle);
  }
  else
    assert (! (h->state & HANDLE_OPEN));
  reset_b_conn_handle (h);
  if (b->i)
    backend_close (b->next, conn);
}

bool
backend_valid_range (struct backend *b, struct connection *conn,
                     uint64_t offset, uint32_t count)
{
  struct b_conn_handle *h = &conn->handles[b->i];

  assert (h->exportsize <= INT64_MAX); /* Guaranteed by negotiation phase */
  return count > 0 && offset <= h->exportsize &&
    offset + count <= h->exportsize;
}

/* Wrappers for all callbacks in a filter's struct nbdkit_next_ops. */

int
backend_reopen (struct backend *b, struct connection *conn, int readonly)
{
  debug ("%s: reopen readonly=%d", b->name, readonly);

  if (backend_finalize (b, conn) == -1)
    return -1;
  backend_close (b, conn);
  if (backend_open (b, conn, readonly) == -1) {
    backend_close (b, conn);
    return -1;
  }
  if (backend_prepare (b, conn) == -1) {
    backend_finalize (b, conn);
    backend_close (b, conn);
    return -1;
  }
  return 0;
}

int64_t
backend_get_size (struct backend *b, struct connection *conn)
{
  struct b_conn_handle *h = &conn->handles[b->i];

  debug ("%s: get_size", b->name);

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->exportsize == -1)
    h->exportsize = b->get_size (b, conn, h->handle);
  return h->exportsize;
}

int
backend_can_write (struct backend *b, struct connection *conn)
{
  struct b_conn_handle *h = &conn->handles[b->i];

  debug ("%s: can_write", b->name);

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->can_write == -1)
    h->can_write = b->can_write (b, conn, h->handle);
  return h->can_write;
}

int
backend_can_flush (struct backend *b, struct connection *conn)
{
  struct b_conn_handle *h = &conn->handles[b->i];

  debug ("%s: can_flush", b->name);

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->can_flush == -1)
    h->can_flush = b->can_flush (b, conn, h->handle);
  return h->can_flush;
}

int
backend_is_rotational (struct backend *b, struct connection *conn)
{
  struct b_conn_handle *h = &conn->handles[b->i];

  debug ("%s: is_rotational", b->name);

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->is_rotational == -1)
    h->is_rotational = b->is_rotational (b, conn, h->handle);
  return h->is_rotational;
}

int
backend_can_trim (struct backend *b, struct connection *conn)
{
  struct b_conn_handle *h = &conn->handles[b->i];
  int r;

  debug ("%s: can_trim", b->name);

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->can_trim == -1) {
    r = backend_can_write (b, conn);
    if (r != 1) {
      h->can_trim = 0;
      return r;
    }
    h->can_trim = b->can_trim (b, conn, h->handle);
  }
  return h->can_trim;
}

int
backend_can_zero (struct backend *b, struct connection *conn)
{
  struct b_conn_handle *h = &conn->handles[b->i];
  int r;

  debug ("%s: can_zero", b->name);

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->can_zero == -1) {
    r = backend_can_write (b, conn);
    if (r != 1) {
      h->can_zero = NBDKIT_ZERO_NONE;
      return r; /* Relies on 0 == NBDKIT_ZERO_NONE */
    }
    h->can_zero = b->can_zero (b, conn, h->handle);
  }
  return h->can_zero;
}

int
backend_can_fast_zero (struct backend *b, struct connection *conn)
{
  struct b_conn_handle *h = &conn->handles[b->i];
  int r;

  debug ("%s: can_fast_zero", b->name);

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->can_fast_zero == -1) {
    r = backend_can_zero (b, conn);
    if (r < NBDKIT_ZERO_EMULATE) {
      h->can_fast_zero = 0;
      return r; /* Relies on 0 == NBDKIT_ZERO_NONE */
    }
    h->can_fast_zero = b->can_fast_zero (b, conn, h->handle);
  }
  return h->can_fast_zero;
}

int
backend_can_extents (struct backend *b, struct connection *conn)
{
  struct b_conn_handle *h = &conn->handles[b->i];

  debug ("%s: can_extents", b->name);

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->can_extents == -1)
    h->can_extents = b->can_extents (b, conn, h->handle);
  return h->can_extents;
}

int
backend_can_fua (struct backend *b, struct connection *conn)
{
  struct b_conn_handle *h = &conn->handles[b->i];
  int r;

  debug ("%s: can_fua", b->name);

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->can_fua == -1) {
    r = backend_can_write (b, conn);
    if (r != 1) {
      h->can_fua = NBDKIT_FUA_NONE;
      return r; /* Relies on 0 == NBDKIT_FUA_NONE */
    }
    h->can_fua = b->can_fua (b, conn, h->handle);
  }
  return h->can_fua;
}

int
backend_can_multi_conn (struct backend *b, struct connection *conn)
{
  struct b_conn_handle *h = &conn->handles[b->i];

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  debug ("%s: can_multi_conn", b->name);

  if (h->can_multi_conn == -1)
    h->can_multi_conn = b->can_multi_conn (b, conn, h->handle);
  return h->can_multi_conn;
}

int
backend_can_cache (struct backend *b, struct connection *conn)
{
  struct b_conn_handle *h = &conn->handles[b->i];

  debug ("%s: can_cache", b->name);

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->can_cache == -1)
    h->can_cache = b->can_cache (b, conn, h->handle);
  return h->can_cache;
}

int
backend_pread (struct backend *b, struct connection *conn,
               void *buf, uint32_t count, uint64_t offset,
               uint32_t flags, int *err)
{
  struct b_conn_handle *h = &conn->handles[b->i];
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  assert (backend_valid_range (b, conn, offset, count));
  assert (flags == 0);
  debug ("%s: pread count=%" PRIu32 " offset=%" PRIu64,
         b->name, count, offset);

  r = b->pread (b, conn, h->handle, buf, count, offset, flags, err);
  if (r == -1)
    assert (*err);
  return r;
}

int
backend_pwrite (struct backend *b, struct connection *conn,
                const void *buf, uint32_t count, uint64_t offset,
                uint32_t flags, int *err)
{
  struct b_conn_handle *h = &conn->handles[b->i];
  bool fua = !!(flags & NBDKIT_FLAG_FUA);
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  assert (h->can_write == 1);
  assert (backend_valid_range (b, conn, offset, count));
  assert (!(flags & ~NBDKIT_FLAG_FUA));
  if (fua)
    assert (h->can_fua > NBDKIT_FUA_NONE);
  debug ("%s: pwrite count=%" PRIu32 " offset=%" PRIu64 " fua=%d",
         b->name, count, offset, fua);

  r = b->pwrite (b, conn, h->handle, buf, count, offset, flags, err);
  if (r == -1)
    assert (*err);
  return r;
}

int
backend_flush (struct backend *b, struct connection *conn,
               uint32_t flags, int *err)
{
  struct b_conn_handle *h = &conn->handles[b->i];
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  assert (h->can_flush == 1);
  assert (flags == 0);
  debug ("%s: flush", b->name);

  r = b->flush (b, conn, h->handle, flags, err);
  if (r == -1)
    assert (*err);
  return r;
}

int
backend_trim (struct backend *b, struct connection *conn,
              uint32_t count, uint64_t offset, uint32_t flags,
              int *err)
{
  struct b_conn_handle *h = &conn->handles[b->i];
  bool fua = !!(flags & NBDKIT_FLAG_FUA);
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  assert (h->can_write == 1);
  assert (h->can_trim == 1);
  assert (backend_valid_range (b, conn, offset, count));
  assert (!(flags & ~NBDKIT_FLAG_FUA));
  if (fua)
    assert (h->can_fua > NBDKIT_FUA_NONE);
  debug ("%s: trim count=%" PRIu32 " offset=%" PRIu64 " fua=%d",
         b->name, count, offset, fua);

  r = b->trim (b, conn, h->handle, count, offset, flags, err);
  if (r == -1)
    assert (*err);
  return r;
}

int
backend_zero (struct backend *b, struct connection *conn,
              uint32_t count, uint64_t offset, uint32_t flags,
              int *err)
{
  struct b_conn_handle *h = &conn->handles[b->i];
  bool fua = !!(flags & NBDKIT_FLAG_FUA);
  bool fast = !!(flags & NBDKIT_FLAG_FAST_ZERO);
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  assert (h->can_write == 1);
  assert (h->can_zero > NBDKIT_ZERO_NONE);
  assert (backend_valid_range (b, conn, offset, count));
  assert (!(flags & ~(NBDKIT_FLAG_MAY_TRIM | NBDKIT_FLAG_FUA |
                      NBDKIT_FLAG_FAST_ZERO)));
  if (fua)
    assert (h->can_fua > NBDKIT_FUA_NONE);
  if (fast)
    assert (h->can_fast_zero == 1);
  debug ("%s: zero count=%" PRIu32 " offset=%" PRIu64
         " may_trim=%d fua=%d fast=%d",
         b->name, count, offset, !!(flags & NBDKIT_FLAG_MAY_TRIM), fua, fast);

  r = b->zero (b, conn, h->handle, count, offset, flags, err);
  if (r == -1) {
    assert (*err);
    if (!fast)
      assert (*err != ENOTSUP && *err != EOPNOTSUPP);
  }
  return r;
}

int
backend_extents (struct backend *b, struct connection *conn,
                 uint32_t count, uint64_t offset, uint32_t flags,
                 struct nbdkit_extents *extents, int *err)
{
  struct b_conn_handle *h = &conn->handles[b->i];
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  assert (h->can_extents >= 0);
  assert (backend_valid_range (b, conn, offset, count));
  assert (!(flags & ~NBDKIT_FLAG_REQ_ONE));
  debug ("%s: extents count=%" PRIu32 " offset=%" PRIu64 " req_one=%d",
         b->name, count, offset, !!(flags & NBDKIT_FLAG_REQ_ONE));

  if (h->can_extents == 0) {
    /* By default it is safe assume that everything in the range is
     * allocated.
     */
    r = nbdkit_add_extent (extents, offset, count, 0 /* allocated data */);
    if (r == -1)
      *err = errno;
    return r;
  }
  r = b->extents (b, conn, h->handle, count, offset, flags, extents, err);
  if (r == -1)
    assert (*err);
  return r;
}

int
backend_cache (struct backend *b, struct connection *conn,
               uint32_t count, uint64_t offset,
               uint32_t flags, int *err)
{
  struct b_conn_handle *h = &conn->handles[b->i];
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  assert (h->can_cache > NBDKIT_CACHE_NONE);
  assert (backend_valid_range (b, conn, offset, count));
  assert (flags == 0);
  debug ("%s: cache count=%" PRIu32 " offset=%" PRIu64,
         b->name, count, offset);

  if (h->can_cache == NBDKIT_CACHE_EMULATE) {
    static char buf[MAX_REQUEST_SIZE]; /* data sink, never read */
    uint32_t limit;

    while (count) {
      limit = MIN (count, sizeof buf);
      if (backend_pread (b, conn, buf, limit, offset, flags, err) == -1)
        return -1;
      count -= limit;
    }
    return 0;
  }
  r = b->cache (b, conn, h->handle, count, offset, flags, err);
  if (r == -1)
    assert (*err);
  return r;
}
