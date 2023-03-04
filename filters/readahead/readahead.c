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
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include <nbdkit-filter.h>

#include "readahead.h"

#include "cleanup.h"
#include "minmax.h"
#include "vector.h"

/* These could be made configurable in future. */
#define READAHEAD_MIN 32768
#define READAHEAD_MAX (4*1024*1024)

/* Size of the readahead window. */
static pthread_mutex_t window_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t window = READAHEAD_MIN;
static uint64_t last_offset = 0, last_readahead = 0;

static int thread_model = -1; /* Thread model of the underlying plugin. */

/* Per-connection data. */
struct readahead_handle {
  int can_cache;      /* Can the underlying plugin cache? */
  pthread_t thread;   /* The background thread, one per connection. */
  struct bgthread_ctrl ctrl;
};

/* We have various requirements of the underlying filter(s) + plugin:
 * - They must support NBDKIT_CACHE_NATIVE (otherwise our requests
 *   would not do anything useful).
 * - They must use the PARALLEL thread model (otherwise we could
 *   violate their thread model).
 */
static bool
filter_working (struct readahead_handle *h)
{
  return
    h->can_cache == NBDKIT_CACHE_NATIVE &&
    thread_model == NBDKIT_THREAD_MODEL_PARALLEL;
}

static bool
suggest_cache_filter (struct readahead_handle *h)
{
  return
    h->can_cache != NBDKIT_CACHE_NATIVE &&
    thread_model == NBDKIT_THREAD_MODEL_PARALLEL;
}

/* We need to hook into .get_ready() so we can read the final thread
 * model (of the whole server).
 */
static int
readahead_get_ready (int final_thread_model)
{
  thread_model = final_thread_model;
  return 0;
}

static int
send_command_to_background_thread (struct bgthread_ctrl *ctrl,
                                   const struct command cmd)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&ctrl->lock);
  if (command_queue_append (&ctrl->cmds, cmd) == -1)
    return -1;
  /* Signal the thread if it could be sleeping on an empty queue. */
  if (ctrl->cmds.len == 1)
    pthread_cond_signal (&ctrl->cond);
  return 0;
}

static void *
readahead_open (nbdkit_next_open *next, nbdkit_context *nxdata,
                int readonly, const char *exportname, int is_tls)
{
  struct readahead_handle *h;
  int err;

  if (next (nxdata, readonly, exportname) == -1)
    return NULL;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  h->ctrl.cmds = (command_queue) empty_vector;
  pthread_mutex_init (&h->ctrl.lock, NULL);
  pthread_cond_init (&h->ctrl.cond, NULL);

  /* Create the background thread. */
  err = pthread_create (&h->thread, NULL, readahead_thread, &h->ctrl);
  if (err != 0) {
    errno = err;
    nbdkit_error ("pthread_create: %m");
    pthread_cond_destroy (&h->ctrl.cond);
    pthread_mutex_destroy (&h->ctrl.lock);
    free (h);
    return NULL;
  }

  return h;
}

static void
readahead_close (void *handle)
{
  struct readahead_handle *h = handle;
  const struct command quit_cmd = { .type = CMD_QUIT };

  send_command_to_background_thread (&h->ctrl, quit_cmd);
  pthread_join (h->thread, NULL);
  pthread_cond_destroy (&h->ctrl.cond);
  pthread_mutex_destroy (&h->ctrl.lock);
  command_queue_reset (&h->ctrl.cmds);
  free (h);
}

static int
readahead_can_cache (nbdkit_next *next, void *handle)
{
  struct readahead_handle *h = handle;
  int r;

  /* Call next->can_cache to read the underlying 'can_cache'. */
  r = next->can_cache (next);
  if (r == -1)
    return -1;
  h->can_cache = r;

  if (!filter_working (h)) {
    nbdkit_error ("readahead: warning: underlying plugin does not support "
                  "NBD_CMD_CACHE or PARALLEL thread model, so the filter "
                  "won't do anything");
    if (suggest_cache_filter (h))
      nbdkit_error ("readahead: try adding --filter=cache "
                    "after this filter");
    /* This is an error, but that's just to ensure that the warning
     * above is seen.  We don't need to return -1 here.
     */
  }

  return r;
}

/* Read data. */
static int
readahead_pread (nbdkit_next *next,
                 void *handle, void *buf, uint32_t count, uint64_t offset,
                 uint32_t flags, int *err)
{
  struct readahead_handle *h = handle;

  /* If the underlying plugin doesn't support caching then skip that
   * step completely.  The filter will do nothing.
   */
  if (filter_working (h)) {
    struct command ra_cmd = { .type = CMD_CACHE, .next = NULL };
    int64_t size;

    size = next->get_size (next);
    if (size >= 0) {
      ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&window_lock);

      /* Generate the asynchronous (background) cache command for
       * the readahead window.
       */
      ra_cmd.offset = offset + count;
      if (ra_cmd.offset < size) {
        ra_cmd.count = MIN (window, size - ra_cmd.offset);
        ra_cmd.next = next; /* If .next is non-NULL, we'll send it below. */
      }

      /* Should we change the window size?
       * If the last readahead < current offset, double the window.
       * If not, but we're still making forward progress, keep the window.
       * If we're not making forward progress, reduce the window to minimum.
       */
      if (last_readahead < offset)
        window = MIN (window * 2, READAHEAD_MAX);
      else if (last_offset < offset)
        /* leave window unchanged */ ;
      else
        window = READAHEAD_MIN;
      last_offset = offset;
      last_readahead = ra_cmd.offset + ra_cmd.count;
    }

    if (ra_cmd.next &&
        send_command_to_background_thread (&h->ctrl, ra_cmd) == -1)
      return -1;
  }

  /* Issue the synchronous read. */
  return next->pread (next, buf, count, offset, flags, err);
}

static struct nbdkit_filter filter = {
  .name              = "readahead",
  .longname          = "nbdkit readahead filter",
  .get_ready         = readahead_get_ready,
  .open              = readahead_open,
  .close             = readahead_close,
  .can_cache         = readahead_can_cache,
  .pread             = readahead_pread,
};

NBDKIT_REGISTER_FILTER (filter)
