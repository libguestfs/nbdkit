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
#include <pthread.h>

#include <nbdkit-filter.h>

#include "scan.h"

#include "cleanup.h"
#include "minmax.h"

static pthread_mutex_t clock_lock;
static uint64_t clock_ = 0;

static void
adjust_clock (uint64_t offset)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&clock_lock);
  if (clock_ < offset)
    clock_ = offset;
}

static void
reset_clock (uint64_t offset)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&clock_lock);
  clock_ = 0;
}

static uint64_t
get_starting_offset (void)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&clock_lock);
  return scan_clock ? clock_ : 0;
}

void *
scan_thread (void *vp)
{
  struct bgthread_ctrl *ctrl = vp;
  uint64_t offset, size;
  int64_t r;

  assert (ctrl->next != NULL);

  /* Get the size of the underlying plugin.  Exit the thread on error
   * because there's not much we can do without knowing the size.
   */
  r = ctrl->next->get_size (ctrl->next);
  if (r == -1)
    return NULL;
  size = r;

  /* Start scanning. */
 start:
  for (offset = get_starting_offset (); offset < size; offset += scan_size) {
    uint64_t n;

    /* Execute any commands in the queue. */
    {
      ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&ctrl->lock);
      struct command cmd;

      while (ctrl->cmds.len) {
        cmd = ctrl->cmds.ptr[0];
        command_queue_remove (&ctrl->cmds, 0);

        switch (cmd.type) {
        case CMD_QUIT:
          nbdkit_debug ("scan: exiting background thread on connection close");
          return NULL;

        case CMD_NOTIFY_PREAD:
          if (offset < cmd.offset)
            offset = cmd.offset;
        }
      }
    }

    adjust_clock (offset);

    if (offset < size) {
      /* Issue the next prefetch. */
      n = MIN (scan_size, size - offset);
      ctrl->next->cache (ctrl->next, n, offset, 0, NULL);
    }
  }

  if (scan_forever) {
    reset_clock (offset);
    goto start;
  }

  nbdkit_debug ("scan: finished scanning the plugin");
  return NULL;
}
