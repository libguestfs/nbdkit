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

#include "readahead.h"

#include "cleanup.h"

void *
readahead_thread (void *vp)
{
  struct bgthread_ctrl *ctrl = vp;

  for (;;) {
    struct command cmd;

    /* Wait until we are sent at least one command. */
    {
      ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&ctrl->lock);
      while (ctrl->cmds.len == 0)
        pthread_cond_wait (&ctrl->cond, &ctrl->lock);
      cmd = ctrl->cmds.ptr[0];
      command_queue_remove (&ctrl->cmds, 0);
    }

    switch (cmd.type) {
    case CMD_QUIT:
      /* Finish processing and exit the thread. */
      return NULL;

    case CMD_CACHE:
      /* Issue .cache (readahead) to underlying plugin.  We ignore any
       * errors because there's no way to communicate that back to the
       * client, and readahead is only advisory.
       */
      cmd.next->cache (cmd.next, cmd.count, cmd.offset, 0, NULL);
    }
  }
}
