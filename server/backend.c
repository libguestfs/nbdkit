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

#include <assert.h>
#include <ctype.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "internal.h"

/* Helpers for registering a new backend. */

/* Set all debug flags which apply to this backend. */
static void
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
      flag->used = true;
    }
  }
}

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

  /* Set debug flags before calling load. */
  set_debug_flags (b->dl, name);

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

void
backend_set_handle (struct backend *b, struct connection *conn, void *handle)
{
  assert (b->i < conn->nr_handles);
  conn->handles[b->i].handle = handle;
}

/* Wrappers for all callbacks in a filter's struct nbdkit_next_ops. */

int64_t
backend_get_size (struct backend *b, struct connection *conn)
{
  struct b_conn_handle *h = &conn->handles[b->i];

  debug ("%s: get_size", b->name);

  if (h->exportsize == -1)
    h->exportsize = b->get_size (b, conn);
  return h->exportsize;
}

int
backend_can_write (struct backend *b, struct connection *conn)
{
  struct b_conn_handle *h = &conn->handles[b->i];

  debug ("%s: can_write", b->name);

  if (h->can_write == -1) {
    /* Special case for outermost backend when -r is in effect. */
    if (readonly && b == backend)
      return h->can_write = 0;
    h->can_write = b->can_write (b, conn);
  }
  return h->can_write;
}

int
backend_can_flush (struct backend *b, struct connection *conn)
{
  debug ("%s: can_flush", b->name);

  return b->can_flush (b, conn);
}

int
backend_is_rotational (struct backend *b, struct connection *conn)
{
  debug ("%s: is_rotational", b->name);

  return b->is_rotational (b, conn);
}

int
backend_can_trim (struct backend *b, struct connection *conn)
{
  int r;

  debug ("%s: can_trim", b->name);

  r = backend_can_write (b, conn);
  if (r != 1)
    return r;
  return b->can_trim (b, conn);
}

int
backend_can_zero (struct backend *b, struct connection *conn)
{
  int r;

  debug ("%s: can_zero", b->name);

  r = backend_can_write (b, conn);
  if (r != 1)
    return r;
  return b->can_zero (b, conn);
}

int
backend_can_extents (struct backend *b, struct connection *conn)
{
  debug ("%s: can_extents", b->name);

  return b->can_extents (b, conn);
}

int
backend_can_fua (struct backend *b, struct connection *conn)
{
  int r;

  debug ("%s: can_fua", b->name);

  r = backend_can_write (b, conn);
  if (r != 1)
    return r; /* Relies on 0 == NBDKIT_FUA_NONE */
  return b->can_fua (b, conn);
}

int
backend_can_multi_conn (struct backend *b, struct connection *conn)
{
  debug ("%s: can_multi_conn", b->name);

  return b->can_multi_conn (b, conn);
}

int
backend_can_cache (struct backend *b, struct connection *conn)
{
  debug ("%s: can_cache", b->name);

  return b->can_cache (b, conn);
}

int
backend_pread (struct backend *b, struct connection *conn,
               void *buf, uint32_t count, uint64_t offset,
               uint32_t flags, int *err)
{
  int r;

  assert (flags == 0);
  debug ("%s: pread count=%" PRIu32 " offset=%" PRIu64,
         b->name, count, offset);

  r = b->pread (b, conn, buf, count, offset, flags, err);
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
  int r;

  assert (h->can_write == 1);
  assert (!(flags & ~NBDKIT_FLAG_FUA));
  debug ("%s: pwrite count=%" PRIu32 " offset=%" PRIu64 " fua=%d",
         b->name, count, offset, !!(flags & NBDKIT_FLAG_FUA));

  r = b->pwrite (b, conn, buf, count, offset, flags, err);
  if (r == -1)
    assert (*err);
  return r;
}

int
backend_flush (struct backend *b, struct connection *conn,
               uint32_t flags, int *err)
{
  int r;

  assert (flags == 0);
  debug ("%s: flush", b->name);

  r = b->flush (b, conn, flags, err);
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
  int r;

  assert (h->can_write == 1);
  assert (flags == 0);
  debug ("%s: trim count=%" PRIu32 " offset=%" PRIu64 " fua=%d",
         b->name, count, offset, !!(flags & NBDKIT_FLAG_FUA));

  r = b->trim (b, conn, count, offset, flags, err);
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
  int r;

  assert (h->can_write == 1);
  assert (!(flags & ~(NBDKIT_FLAG_MAY_TRIM | NBDKIT_FLAG_FUA)));
  debug ("%s: zero count=%" PRIu32 " offset=%" PRIu64 " may_trim=%d fua=%d",
         b->name, count, offset, !!(flags & NBDKIT_FLAG_MAY_TRIM),
         !!(flags & NBDKIT_FLAG_FUA));

  r = b->zero (b, conn, count, offset, flags, err);
  if (r == -1)
    assert (*err && *err != ENOTSUP && *err != EOPNOTSUPP);
  return r;
}

int
backend_extents (struct backend *b, struct connection *conn,
                 uint32_t count, uint64_t offset, uint32_t flags,
                 struct nbdkit_extents *extents, int *err)
{
  int r;

  assert (!(flags & ~NBDKIT_FLAG_REQ_ONE));
  debug ("%s: extents count=%" PRIu32 " offset=%" PRIu64 " req_one=%d",
         b->name, count, offset, !!(flags & NBDKIT_FLAG_REQ_ONE));

  r = b->extents (b, conn, count, offset, flags, extents, err);
  if (r == -1)
    assert (*err);
  return r;
}

int
backend_cache (struct backend *b, struct connection *conn,
               uint32_t count, uint64_t offset,
               uint32_t flags, int *err)
{
  int r;

  assert (flags == 0);
  debug ("%s: cache count=%" PRIu32 " offset=%" PRIu64,
         b->name, count, offset);

  r = b->cache (b, conn, count, offset, flags, err);
  if (r == -1)
    assert (*err);
  return r;
}
