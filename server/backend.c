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
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include <dlfcn.h>

#include "internal.h"
#include "minmax.h"

/* Helpers for registering a new backend. */

/* Use:
 * -D nbdkit.backend.controlpath=0 to suppress control path debugging.
 * -D nbdkit.backend.datapath=0 to suppress data path debugging.
 */
int nbdkit_debug_backend_controlpath = 1;
int nbdkit_debug_backend_datapath = 1;

#define controlpath_debug(fs, ...)                                     \
  do {                                                                 \
    if (nbdkit_debug_backend_controlpath) debug ((fs), ##__VA_ARGS__); \
  } while (0)
#define datapath_debug(fs, ...)                                        \
  do {                                                                 \
    if (nbdkit_debug_backend_datapath) debug ((fs), ##__VA_ARGS__);    \
  } while (0)

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
  controlpath_debug ("%s: load", name);
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

  controlpath_debug ("%s: unload %s", b->name, b->type);
  if (unload)
    unload ();

  if (DO_DLCLOSE)
    dlclose (b->dl);
  free (b->filename);

  unlock_unload ();

  free (b->name);
}

int
backend_open (struct backend *b, int readonly)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);

  controlpath_debug ("%s: open readonly=%d", b->name, readonly);

  assert (h->handle == NULL);
  assert ((h->state & HANDLE_OPEN) == 0);
  assert (h->can_write == -1);
  if (readonly)
    h->can_write = 0;

  /* Most filters will call next_open first, resulting in
   * inner-to-outer ordering.
   */
  h->handle = b->open (b, readonly);
  controlpath_debug ("%s: open returned handle %p", b->name, h->handle);

  if (h->handle == NULL) {
    if (b->i) /* Do not strand backend if this layer failed */
      backend_close (b->next);
    return -1;
  }

  h->state |= HANDLE_OPEN;
  if (b->i) /* A filter must not succeed unless its backend did also */
    assert (get_handle (conn, b->i-1)->handle != NULL);
  return 0;
}

int
backend_prepare (struct backend *b)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);

  assert (h->handle);
  assert ((h->state & (HANDLE_OPEN | HANDLE_CONNECTED)) == HANDLE_OPEN);

  /* Call these in order starting from the filter closest to the
   * plugin, similar to typical .open order.
   */
  if (b->i && backend_prepare (b->next) == -1)
    return -1;

  controlpath_debug ("%s: prepare readonly=%d", b->name, h->can_write == 0);

  if (b->prepare (b, h->handle, h->can_write == 0) == -1)
    return -1;
  h->state |= HANDLE_CONNECTED;
  return 0;
}

int
backend_finalize (struct backend *b)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);

  /* Call these in reverse order to .prepare above, starting from the
   * filter furthest away from the plugin, and matching .close order.
   */

  /* Once finalize fails, we can do nothing further on this connection */
  if (h->state & HANDLE_FAILED)
    return -1;

  if (h->state & HANDLE_CONNECTED) {
    assert (h->state & HANDLE_OPEN && h->handle);
    controlpath_debug ("%s: finalize", b->name);
    if (b->finalize (b, h->handle) == -1) {
      h->state |= HANDLE_FAILED;
      return -1;
    }
  }

  if (b->i)
    return backend_finalize (b->next);
  return 0;
}

void
backend_close (struct backend *b)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);

  /* outer-to-inner order, opposite .open */

  if (h->handle) {
    assert (h->state & HANDLE_OPEN);
    controlpath_debug ("%s: close", b->name);
    b->close (b, h->handle);
  }
  else
    assert (! (h->state & HANDLE_OPEN));
  reset_handle (h);
  if (b->i)
    backend_close (b->next);
}

bool
backend_valid_range (struct backend *b, uint64_t offset, uint32_t count)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);

  assert (h->exportsize <= INT64_MAX); /* Guaranteed by negotiation phase */
  return count > 0 && offset <= h->exportsize &&
    offset + count <= h->exportsize;
}

/* Wrappers for all callbacks in a filter's struct nbdkit_next_ops. */

int
backend_reopen (struct backend *b, int readonly)
{
  controlpath_debug ("%s: reopen readonly=%d", b->name, readonly);

  if (backend_finalize (b) == -1)
    return -1;
  backend_close (b);
  if (backend_open (b, readonly) == -1) {
    backend_close (b);
    return -1;
  }
  if (backend_prepare (b) == -1) {
    backend_finalize (b);
    backend_close (b);
    return -1;
  }
  return 0;
}

int64_t
backend_get_size (struct backend *b)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->exportsize == -1) {
    controlpath_debug ("%s: get_size", b->name);
    h->exportsize = b->get_size (b, h->handle);
  }
  return h->exportsize;
}

int
backend_can_write (struct backend *b)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->can_write == -1) {
    controlpath_debug ("%s: can_write", b->name);
    h->can_write = b->can_write (b, h->handle);
  }
  return h->can_write;
}

int
backend_can_flush (struct backend *b)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->can_flush == -1) {
    controlpath_debug ("%s: can_flush", b->name);
    h->can_flush = b->can_flush (b, h->handle);
  }
  return h->can_flush;
}

int
backend_is_rotational (struct backend *b)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->is_rotational == -1) {
    controlpath_debug ("%s: is_rotational", b->name);
    h->is_rotational = b->is_rotational (b, h->handle);
  }
  return h->is_rotational;
}

int
backend_can_trim (struct backend *b)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->can_trim == -1) {
    controlpath_debug ("%s: can_trim", b->name);
    r = backend_can_write (b);
    if (r != 1) {
      h->can_trim = 0;
      return r;
    }
    h->can_trim = b->can_trim (b, h->handle);
  }
  return h->can_trim;
}

int
backend_can_zero (struct backend *b)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->can_zero == -1) {
    controlpath_debug ("%s: can_zero", b->name);
    r = backend_can_write (b);
    if (r != 1) {
      h->can_zero = NBDKIT_ZERO_NONE;
      return r; /* Relies on 0 == NBDKIT_ZERO_NONE */
    }
    h->can_zero = b->can_zero (b, h->handle);
  }
  return h->can_zero;
}

int
backend_can_fast_zero (struct backend *b)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->can_fast_zero == -1) {
    controlpath_debug ("%s: can_fast_zero", b->name);
    r = backend_can_zero (b);
    if (r < NBDKIT_ZERO_EMULATE) {
      h->can_fast_zero = 0;
      return r; /* Relies on 0 == NBDKIT_ZERO_NONE */
    }
    h->can_fast_zero = b->can_fast_zero (b, h->handle);
  }
  return h->can_fast_zero;
}

int
backend_can_extents (struct backend *b)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->can_extents == -1) {
    controlpath_debug ("%s: can_extents", b->name);
    h->can_extents = b->can_extents (b, h->handle);
  }
  return h->can_extents;
}

int
backend_can_fua (struct backend *b)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->can_fua == -1) {
    controlpath_debug ("%s: can_fua", b->name);
    r = backend_can_write (b);
    if (r != 1) {
      h->can_fua = NBDKIT_FUA_NONE;
      return r; /* Relies on 0 == NBDKIT_FUA_NONE */
    }
    h->can_fua = b->can_fua (b, h->handle);
  }
  return h->can_fua;
}

int
backend_can_multi_conn (struct backend *b)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->can_multi_conn == -1) {
    controlpath_debug ("%s: can_multi_conn", b->name);
    h->can_multi_conn = b->can_multi_conn (b, h->handle);
  }
  return h->can_multi_conn;
}

int
backend_can_cache (struct backend *b)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  if (h->can_cache == -1) {
    controlpath_debug ("%s: can_cache", b->name);
    h->can_cache = b->can_cache (b, h->handle);
  }
  return h->can_cache;
}

int
backend_pread (struct backend *b,
               void *buf, uint32_t count, uint64_t offset,
               uint32_t flags, int *err)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  assert (backend_valid_range (b, offset, count));
  assert (flags == 0);
  datapath_debug ("%s: pread count=%" PRIu32 " offset=%" PRIu64,
                  b->name, count, offset);

  r = b->pread (b, h->handle, buf, count, offset, flags, err);
  if (r == -1)
    assert (*err);
  return r;
}

int
backend_pwrite (struct backend *b,
                const void *buf, uint32_t count, uint64_t offset,
                uint32_t flags, int *err)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);
  bool fua = !!(flags & NBDKIT_FLAG_FUA);
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  assert (h->can_write == 1);
  assert (backend_valid_range (b, offset, count));
  assert (!(flags & ~NBDKIT_FLAG_FUA));
  if (fua)
    assert (h->can_fua > NBDKIT_FUA_NONE);
  datapath_debug ("%s: pwrite count=%" PRIu32 " offset=%" PRIu64 " fua=%d",
                  b->name, count, offset, fua);

  r = b->pwrite (b, h->handle, buf, count, offset, flags, err);
  if (r == -1)
    assert (*err);
  return r;
}

int
backend_flush (struct backend *b,
               uint32_t flags, int *err)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  assert (h->can_flush == 1);
  assert (flags == 0);
  datapath_debug ("%s: flush", b->name);

  r = b->flush (b, h->handle, flags, err);
  if (r == -1)
    assert (*err);
  return r;
}

int
backend_trim (struct backend *b,
              uint32_t count, uint64_t offset, uint32_t flags,
              int *err)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);
  bool fua = !!(flags & NBDKIT_FLAG_FUA);
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  assert (h->can_write == 1);
  assert (h->can_trim == 1);
  assert (backend_valid_range (b, offset, count));
  assert (!(flags & ~NBDKIT_FLAG_FUA));
  if (fua)
    assert (h->can_fua > NBDKIT_FUA_NONE);
  datapath_debug ("%s: trim count=%" PRIu32 " offset=%" PRIu64 " fua=%d",
                  b->name, count, offset, fua);

  r = b->trim (b, h->handle, count, offset, flags, err);
  if (r == -1)
    assert (*err);
  return r;
}

int
backend_zero (struct backend *b,
              uint32_t count, uint64_t offset, uint32_t flags,
              int *err)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);
  bool fua = !!(flags & NBDKIT_FLAG_FUA);
  bool fast = !!(flags & NBDKIT_FLAG_FAST_ZERO);
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  assert (h->can_write == 1);
  assert (h->can_zero > NBDKIT_ZERO_NONE);
  assert (backend_valid_range (b, offset, count));
  assert (!(flags & ~(NBDKIT_FLAG_MAY_TRIM | NBDKIT_FLAG_FUA |
                      NBDKIT_FLAG_FAST_ZERO)));
  if (fua)
    assert (h->can_fua > NBDKIT_FUA_NONE);
  if (fast)
    assert (h->can_fast_zero == 1);
  datapath_debug ("%s: zero count=%" PRIu32 " offset=%" PRIu64
                  " may_trim=%d fua=%d fast=%d",
                  b->name, count, offset,
                  !!(flags & NBDKIT_FLAG_MAY_TRIM), fua, fast);

  r = b->zero (b, h->handle, count, offset, flags, err);
  if (r == -1) {
    assert (*err);
    if (!fast)
      assert (*err != ENOTSUP && *err != EOPNOTSUPP);
  }
  return r;
}

int
backend_extents (struct backend *b,
                 uint32_t count, uint64_t offset, uint32_t flags,
                 struct nbdkit_extents *extents, int *err)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  assert (h->can_extents >= 0);
  assert (backend_valid_range (b, offset, count));
  assert (!(flags & ~NBDKIT_FLAG_REQ_ONE));
  datapath_debug ("%s: extents count=%" PRIu32 " offset=%" PRIu64 " req_one=%d",
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
  r = b->extents (b, h->handle, count, offset, flags, extents, err);
  if (r == -1)
    assert (*err);
  return r;
}

int
backend_cache (struct backend *b,
               uint32_t count, uint64_t offset,
               uint32_t flags, int *err)
{
  GET_CONN;
  struct handle *h = get_handle (conn, b->i);
  int r;

  assert (h->handle && (h->state & HANDLE_CONNECTED));
  assert (h->can_cache > NBDKIT_CACHE_NONE);
  assert (backend_valid_range (b, offset, count));
  assert (flags == 0);
  datapath_debug ("%s: cache count=%" PRIu32 " offset=%" PRIu64,
                  b->name, count, offset);

  if (h->can_cache == NBDKIT_CACHE_EMULATE) {
    static char buf[MAX_REQUEST_SIZE]; /* data sink, never read */
    uint32_t limit;

    while (count) {
      limit = MIN (count, sizeof buf);
      if (backend_pread (b, buf, limit, offset, flags, err) == -1)
        return -1;
      count -= limit;
    }
    return 0;
  }
  r = b->cache (b, h->handle, count, offset, flags, err);
  if (r == -1)
    assert (*err);
  return r;
}
