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

#include "scan.h"

#include "cleanup.h"
#include "ispowerof2.h"
#include "vector.h"

static bool scan_ahead = true;
bool scan_clock = true;
bool scan_forever = false;
unsigned scan_size = 2*1024*1024;

static int thread_model = -1; /* Thread model of the underlying plugin. */

/* Per-connection data. */
struct scan_handle {
  bool is_default_export;  /* If exportname == "". */
  bool running;            /* True if background thread is running. */
  pthread_t thread;        /* The background thread, one per connection. */
  struct bgthread_ctrl ctrl;
};

static int
scan_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
             const char *key, const char *value)
{
  int r;

  if (strcmp (key, "scan-ahead") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    scan_ahead = r;
    return 0;
  }
  else if (strcmp (key, "scan-clock") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    scan_clock = r;
    return 0;
  }
  else if (strcmp (key, "scan-forever") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    scan_forever = r;
    return 0;
  }
  else if (strcmp (key, "scan-size") == 0) {
    scan_size = nbdkit_parse_size (value);
    if (scan_size == -1)
      return -1;
    return 0;
  }

  return next (nxdata, key, value);
}

static int
scan_config_complete (nbdkit_next_config_complete *next, nbdkit_backend *nxdata)
{
  if (scan_size < 512 || scan_size > 32*1024*1024 ||
      !is_power_of_2 (scan_size)) {
    nbdkit_error ("scan-size parameter should be [512..32M] "
                  "and a power of two");
    return -1;
  }

  return next (nxdata);
}

#define scan_config_help \
  "scan-ahead=false         Skip ahead when client reads faster.\n" \
  "scan-clock=false         Always start prefetching from beginning.\n" \
  "scan-forever=true        Scan in a loop while clients connected.\n" \
  "scan-size=NN             Set scan block size."

/* We need to hook into .get_ready() so we can read the final thread
 * model (of the whole server).
 */
static int
scan_get_ready (int final_thread_model)
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
  return 0;
}

static void *
scan_open (nbdkit_next_open *next, nbdkit_context *nxdata,
           int readonly, const char *exportname, int is_tls)
{
  struct scan_handle *h;

  if (next (nxdata, readonly, exportname) == -1)
    return NULL;

  h = calloc (1, sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  h->is_default_export = strcmp (exportname, "") == 0;
  return h;
}

/* In prepare we check if it's possible to support the scan filter on
 * this connection (or print a warning), and start the background
 * thread.
 */
static int
scan_prepare (nbdkit_next *next, void *handle, int readonly)
{
  struct scan_handle *h = handle;
  int r, err;

  if (!h->is_default_export) {
    nbdkit_error ("scan: warning: not the default export, not scanning");
    return 0;
  }

  if (thread_model != NBDKIT_THREAD_MODEL_PARALLEL) {
    nbdkit_error ("scan: warning: underlying plugin does not support "
                  "the PARALLEL thread model, not scanning");
    return 0;
  }

  /* Call next->can_cache to read the underlying 'can_cache'. */
  r = next->can_cache (next);
  if (r == -1)
    return -1;
  if (r != NBDKIT_CACHE_NATIVE) {
    nbdkit_error ("scan: warning: underlying plugin does not support "
                  "NBD_CMD_CACHE, not scanning; try adding --filter=cache "
                  "after this filter");
    return 0;
  }

  /* Save the connection in the handle, for the background thread to use. */
  h->ctrl.next = next;

  /* Create the background thread. */
  h->ctrl.cmds = (command_queue) empty_vector;
  pthread_mutex_init (&h->ctrl.lock, NULL);

  err = pthread_create (&h->thread, NULL, scan_thread, &h->ctrl);
  if (err != 0) {
    errno = err;
    nbdkit_error ("pthread_create: %m");
    pthread_mutex_destroy (&h->ctrl.lock);
    return -1;
  }

  h->running = true;

  return 0;
}

/* Finalize cleans up the thread if it is running. */
static int
scan_finalize (nbdkit_next *next, void *handle)
{
  struct scan_handle *h = handle;
  const struct command quit_cmd = { .type = CMD_QUIT };

  if (!h->running)
    return 0;

  send_command_to_background_thread (&h->ctrl, quit_cmd);
  pthread_join (h->thread, NULL);
  pthread_mutex_destroy (&h->ctrl.lock);
  command_queue_reset (&h->ctrl.cmds);
  h->running = false;

  return 0;
}

static void
scan_close (void *handle)
{
  struct scan_handle *h = handle;

  free (h);
}

/* Read data. */
static int
scan_pread (nbdkit_next *next,
            void *handle, void *buf, uint32_t count, uint64_t offset,
            uint32_t flags, int *err)
{
  struct scan_handle *h = handle;

  if (scan_ahead && h->running) {
    const struct command cmd =
      { .type = CMD_NOTIFY_PREAD, .offset = offset + count };

    if (send_command_to_background_thread (&h->ctrl, cmd) == -1)
      return -1;
  }

  /* Issue the normal read. */
  return next->pread (next, buf, count, offset, flags, err);
}

static struct nbdkit_filter filter = {
  .name              = "scan",
  .longname          = "nbdkit scan filter",
  .get_ready         = scan_get_ready,
  .config            = scan_config,
  .config_complete   = scan_config_complete,
  .config_help       = scan_config_help,
  .open              = scan_open,
  .prepare           = scan_prepare,
  .finalize          = scan_finalize,
  .close             = scan_close,
  .pread             = scan_pread,
};

NBDKIT_REGISTER_FILTER (filter)
