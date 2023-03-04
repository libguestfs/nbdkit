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
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "vector.h"

/* Track results of .config */
static enum MultiConnMode {
  AUTO,
  EMULATE,
  PLUGIN,
  UNSAFE,
  DISABLE,
} mode;

static enum TrackDirtyMode {
  CONN,
  FAST,
  OFF,
} track;

static bool byname = false;

enum dirty {
  WRITE = 1, /* A write may have populated a cache */
  READ = 2, /* A read may have populated a cache */
};

/* Coordination between connections. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* The list of handles to active connections. */
struct handle {
  nbdkit_next *next;
  enum MultiConnMode mode; /* Runtime resolution of mode==AUTO */
  enum dirty dirty; /* What aspects of this connection are dirty */
  char *name; /* Used when byname==true to assign group */
  struct group *group; /* All connections grouped with this one */
};
DEFINE_VECTOR_TYPE (conns_vector, struct handle *);
struct group {
  conns_vector conns;
  char *name;
  bool dirty; /* True if any connection in group is dirty */
};
DEFINE_VECTOR_TYPE (group_vector, struct group *);
static group_vector groups = empty_vector;

/* Accept 'multi-conn-mode=mode', 'multi-conn-track-dirty=level', and
 * 'multi-conn-exportname=bool'.
 */
static int
multi_conn_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
                   const char *key, const char *value)
{
  if (strcmp (key, "multi-conn-mode") == 0) {
    if (strcmp (value, "auto") == 0)
      mode = AUTO;
    else if (strcmp (value, "emulate") == 0)
      mode = EMULATE;
    else if (strcmp (value, "plugin") == 0)
      mode = PLUGIN;
    else if (strcmp (value, "disable") == 0)
      mode = DISABLE;
    else if (strcmp (value, "unsafe") == 0)
      mode = UNSAFE;
    else {
      nbdkit_error ("unknown multi-conn mode '%s'", value);
      return -1;
    }
    return 0;
  }
  else if (strcmp (key, "multi-conn-track-dirty") == 0) {
    if (strcmp (value, "connection") == 0 ||
        strcmp (value, "conn") == 0)
      track = CONN;
    else if (strcmp (value, "fast") == 0)
      track = FAST;
    else if (strcmp (value, "off") == 0)
      track = OFF;
    else {
      nbdkit_error ("unknown multi-conn track-dirty setting '%s'", value);
      return -1;
    }
    return 0;
  }
  else if (strcmp (key, "multi-conn-exportname") == 0 ||
           strcmp (key, "multi-conn-export-name") == 0) {
    int r;

    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    byname = r;
    return 0;
  }
  return next (nxdata, key, value);
}

#define multi_conn_config_help \
  "multi-conn-mode=<MODE>          'auto' (default), 'emulate', 'plugin',\n" \
  "                                'disable', or 'unsafe'.\n" \
  "multi-conn-track-dirty=<LEVEL>  'conn' (default), 'fast', or 'off'.\n" \
  "multi-conn-exportname=<BOOL>    true to limit emulation by export name.\n"

static int
multi_conn_get_ready (int thread_model)
{
  if (thread_model == NBDKIT_THREAD_MODEL_SERIALIZE_CONNECTIONS &&
      mode == AUTO)
    mode = DISABLE;
  return 0;
}

static void
multi_conn_unload (void)
{
  assert (groups.len == 0);
  group_vector_reset (&groups);
}

static void *
multi_conn_open (nbdkit_next_open *next, nbdkit_context *nxdata,
                 int readonly, const char *exportname, int is_tls)
{
  struct handle *h;

  if (next (nxdata, readonly, exportname) == -1)
    return NULL;

  /* Allocate here, but populate and insert into list in .prepare */
  h = calloc (1, sizeof *h);
  if (h == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }
  if (byname) {
    h->name = strdup (exportname);
    if (h->name == NULL) {
      nbdkit_error ("strdup: %m");
      free (h);
      return NULL;
    }
  }
  return h;
}

static int
multi_conn_prepare (nbdkit_next *next, void *handle, int readonly)
{
  struct handle *h = handle;
  struct group *g;
  bool new_group = false;
  int r;
  size_t i;

  h->next = next;
  if (mode == AUTO) { /* See also .get_ready turning AUTO into DISABLE */
    r = next->can_multi_conn (next);
    if (r == -1)
      return -1;
    if (r == 0)
      h->mode = EMULATE;
    else
      h->mode = PLUGIN;
  }
  else
    h->mode = mode;
  if (h->mode == EMULATE && next->can_flush (next) != 1) {
    nbdkit_error ("emulating multi-conn requires working flush");
    return -1;
  }

  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
  if (byname) {
    g = NULL;
    for (i = 0; i < groups.len; i++)
      if (strcmp (groups.ptr[i]->name, h->name) == 0) {
        g = groups.ptr[i];
        break;
      }
  }
  else
    g = groups.len ? groups.ptr[0] : NULL;

  if (!g) {
    g = calloc (1, sizeof *g);
    if (g == NULL) {
      nbdkit_error ("calloc: %m");
      return -1;
    }
    if (group_vector_append (&groups, g) == -1) {
      free (g);
      return -1;
    }
    g->name = h->name;
    h->name = NULL;
    new_group = true;
  }
  if (conns_vector_append (&g->conns, h) == -1) {
    if (new_group) {
      group_vector_remove (&groups, groups.len - 1);
      free (g->name);
      free (g);
    }
    return -1;
  }
  h->group = g;
  return 0;
}

static int
multi_conn_finalize (nbdkit_next *next, void *handle)
{
  struct handle *h = handle;
  size_t i;

  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
  assert (h->next == next);
  assert (h->group);

  /* XXX should we add a config param to flush if the client forgot? */
  for (i = 0; i < h->group->conns.len; i++) {
    if (h->group->conns.ptr[i] == h) {
      conns_vector_remove (&h->group->conns, i);
      break;
    }
  }
  if (h->group->conns.len == 0) {
    for (i = 0; i < groups.len; i++)
      if (groups.ptr[i] == h->group) {
        group_vector_remove (&groups, i);
        free (h->group->name);
        free (h->group);
        break;
      }
  }
  h->group = NULL;
  return 0;
}

static void
multi_conn_close (void *handle)
{
  struct handle *h = handle;

  assert (h->group == NULL);
  free (h->name);
  free (h);
}

static int
multi_conn_can_fua (nbdkit_next *next, void *handle)
{
  /* If the backend has native FUA support but is not multi-conn
   * consistent, and we have to flush on every connection, then we are
   * better off advertising emulated fua rather than native.
   */
  struct handle *h = handle;
  int fua = next->can_fua (next);

  assert (h->mode != AUTO);
  if (fua == NBDKIT_FUA_NATIVE && h->mode == EMULATE)
    return NBDKIT_FUA_EMULATE;
  return fua;
}

static int
multi_conn_can_multi_conn (nbdkit_next *next, void *handle)
{
  struct handle *h = handle;

  switch (h->mode) {
  case EMULATE:
    return 1;
  case PLUGIN:
    return next->can_multi_conn (next);
  case DISABLE:
    return 0;
  case UNSAFE:
    return 1;
  case AUTO: /* Not possible, see .prepare */
  default:
    abort ();
  }
}

static void
mark_dirty (struct handle *h, bool is_write)
{
  /* No need to grab lock here: the NBD spec is clear that a client
   * must wait for the response to a flush before sending the next
   * command that expects to see the result of that flush, so any race
   * in accessing dirty can be traced back to the client improperly
   * sending a flush in parallel with other live commands.
   */
  switch (track) {
  case CONN:
    h->dirty |= is_write ? WRITE : READ;
    /* fallthrough */
  case FAST:
    if (is_write)
      h->group->dirty = true;
    break;
  case OFF:
    break;
  default:
    abort ();
  }
}

static int
multi_conn_flush (nbdkit_next *next,
                  void *handle, uint32_t flags, int *err);

static int
multi_conn_pread (nbdkit_next *next,
                  void *handle, void *buf, uint32_t count, uint64_t offs,
                  uint32_t flags, int *err)
{
  struct handle *h = handle;

  mark_dirty (h, false);
  return next->pread (next, buf, count, offs, flags, err);
}

static int
multi_conn_pwrite (nbdkit_next *next,
                   void *handle, const void *buf, uint32_t count,
                   uint64_t offs, uint32_t flags, int *err)
{
  struct handle *h = handle;
  bool need_flush = false;

  if (flags & NBDKIT_FLAG_FUA) {
    if (h->mode == EMULATE) {
      mark_dirty (h, true);
      need_flush = true;
      flags &= ~NBDKIT_FLAG_FUA;
    }
  }
  else
    mark_dirty (h, true);

  if (next->pwrite (next, buf, count, offs, flags, err) == -1)
    return -1;
  if (need_flush)
    return multi_conn_flush (next, h, 0, err);
  return 0;
}

static int
multi_conn_zero (nbdkit_next *next,
                 void *handle, uint32_t count, uint64_t offs, uint32_t flags,
                 int *err)
{
  struct handle *h = handle;
  bool need_flush = false;

  if (flags & NBDKIT_FLAG_FUA) {
    if (h->mode == EMULATE) {
      mark_dirty (h, true);
      need_flush = true;
      flags &= ~NBDKIT_FLAG_FUA;
    }
  }
  else
    mark_dirty (h, true);

  if (next->zero (next, count, offs, flags, err) == -1)
    return -1;
  if (need_flush)
    return multi_conn_flush (next, h, 0, err);
  return 0;
}

static int
multi_conn_trim (nbdkit_next *next,
                 void *handle, uint32_t count, uint64_t offs, uint32_t flags,
                 int *err)
{
  struct handle *h = handle;
  bool need_flush = false;

  if (flags & NBDKIT_FLAG_FUA) {
    if (h->mode == EMULATE) {
      mark_dirty (h, true);
      need_flush = true;
      flags &= ~NBDKIT_FLAG_FUA;
    }
  }
  else
    mark_dirty (h, true);

  if (next->trim (next, count, offs, flags, err) == -1)
    return -1;
  if (need_flush)
    return multi_conn_flush (next, h, 0, err);
  return 0;
}

static int
multi_conn_cache (nbdkit_next *next,
                  void *handle, uint32_t count, uint64_t offs, uint32_t flags,
                  int *err)
{
  struct handle *h = handle;

  mark_dirty (h, false);
  return next->cache (next, count, offs, flags, err);
}

static int
multi_conn_flush (nbdkit_next *next,
                  void *handle, uint32_t flags, int *err)
{
  struct handle *h = handle, *h2;
  size_t i;

  assert (h->group);
  if (h->mode == EMULATE) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
    for (i = 0; i < h->group->conns.len; i++) {
      h2 = h->group->conns.ptr[i];
      if (track == OFF || (h->group->dirty &&
                           (track == FAST || h2->dirty & READ)) ||
          h2->dirty & WRITE) {
        if (h2->next->flush (h2->next, flags, err) == -1)
          return -1;
        h2->dirty = 0;
      }
    }
    h->group->dirty = 0;
  }
  else {
    /* !EMULATE: Check if the image is clean, allowing us to skip a flush. */
    if (track != OFF && !h->group->dirty)
      return 0;
    /* Perform the flush, then update dirty tracking. */
    if (next->flush (next, flags, err) == -1)
      return -1;
    switch (track) {
    case CONN:
      if (next->can_multi_conn (next) == 1) {
        ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
        for (i = 0; i < h->group->conns.len; i++)
          h->group->conns.ptr[i]->dirty = 0;
        h->group->dirty = 0;
      }
      else
        h->dirty = 0;
      break;
    case FAST:
      h->group->dirty = false;
      break;
    case OFF:
      break;
    default:
      abort ();
    }
  }
  return 0;
}

static struct nbdkit_filter filter = {
  .name              = "multi-conn",
  .longname          = "nbdkit multi-conn filter",
  .config            = multi_conn_config,
  .config_help       = multi_conn_config_help,
  .get_ready         = multi_conn_get_ready,
  .unload            = multi_conn_unload,
  .open              = multi_conn_open,
  .prepare           = multi_conn_prepare,
  .finalize          = multi_conn_finalize,
  .close             = multi_conn_close,
  .can_fua           = multi_conn_can_fua,
  .can_multi_conn    = multi_conn_can_multi_conn,
  .pread             = multi_conn_pread,
  .pwrite            = multi_conn_pwrite,
  .trim              = multi_conn_trim,
  .zero              = multi_conn_zero,
  .cache             = multi_conn_cache,
  .flush             = multi_conn_flush,
};

NBDKIT_REGISTER_FILTER (filter)
