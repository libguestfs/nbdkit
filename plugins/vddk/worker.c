/* nbdkit
 * Copyright (C) 2013-2021 Red Hat Inc.
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
#include <inttypes.h>

#include <pthread.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "minmax.h"
#include "rounding.h"
#include "vector.h"

#include "vddk.h"

const char *
command_type_string (enum command_type type)
{
  switch (type) {
  case GET_SIZE:    return "get_size";
  case READ:        return "read";
  case WRITE:       return "write";
  case FLUSH:       return "flush";
  case CAN_EXTENTS: return "can_extents";
  case EXTENTS:     return "extents";
  case STOP:        return "stop";
  default:          abort ();
  }
}

/* Send command to the background thread and wait for completion.
 *
 * Returns 0 for OK
 * On error, calls nbdkit_error and returns -1.
 */
int
send_command_and_wait (struct vddk_handle *h, struct command *cmd)
{
  /* Add the command to the command queue. */
  {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&h->commands_lock);
    cmd->id = h->id++;

    if (command_queue_append (&h->commands, cmd) == -1)
      /* On error command_queue_append will call nbdkit_error. */
      return -1;

    /* Signal the caller if it could be sleeping on an empty queue. */
    if (h->commands.size == 1)
      pthread_cond_signal (&h->commands_cond);

    /* This will be used to signal command completion back to us. */
    pthread_mutex_init (&cmd->mutex, NULL);
    pthread_cond_init (&cmd->cond, NULL);
  }

  /* Wait for the command to be completed by the background thread. */
  {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&cmd->mutex);
    while (cmd->status == SUBMITTED)
      pthread_cond_wait (&cmd->cond, &cmd->mutex);
  }

  pthread_mutex_destroy (&cmd->mutex);
  pthread_cond_destroy (&cmd->cond);

  /* On error the background thread will call nbdkit_error. */
  switch (cmd->status) {
  case SUCCEEDED: return 0;
  case FAILED:    return -1;
  default:        abort ();
  }
}

/* Asynchronous commands are completed when this function is called. */
static void
complete_command (void *vp, VixError result)
{
  struct command *cmd = vp;

  if (vddk_debug_datapath)
    nbdkit_debug ("command %" PRIu64 " completed", cmd->id);

  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&cmd->mutex);

  if (result == VIX_OK) {
    cmd->status = SUCCEEDED;
  } else {
    VDDK_ERROR (result, "command %" PRIu64 ": asynchronous %s failed",
                cmd->id, command_type_string (cmd->type));
    cmd->status = FAILED;
  }

  pthread_cond_signal (&cmd->cond);
}

/* Wait for any asynchronous commands to complete. */
static int
do_stop (struct command *cmd, struct vddk_handle *h)
{
  VixError err;

  /* Because we assume VDDK >= 6.5, VixDiskLib_Wait must exist. */
  VDDK_CALL_START (VixDiskLib_Wait, "handle")
    err = VixDiskLib_Wait (h->handle);
  VDDK_CALL_END (VixDiskLib_Wait, 0);
  if (err != VIX_OK) {
    VDDK_ERROR (err, "VixDiskLib_Wait");
    /* In the end this error indication is ignored because it only
     * happens on the close path when we cannot handle errors.
     */
    return -1;
  }
  return 0;
}

/* Get size command. */
static int64_t
do_get_size (struct command *cmd, struct vddk_handle *h)
{
  VixError err;
  VixDiskLibInfo *info;
  uint64_t size;

  VDDK_CALL_START (VixDiskLib_GetInfo, "handle, &info")
    err = VixDiskLib_GetInfo (h->handle, &info);
  VDDK_CALL_END (VixDiskLib_GetInfo, 0);
  if (err != VIX_OK) {
    VDDK_ERROR (err, "VixDiskLib_GetInfo");
    return -1;
  }

  size = info->capacity * (uint64_t)VIXDISKLIB_SECTOR_SIZE;

  if (vddk_debug_diskinfo) {
    nbdkit_debug ("disk info: capacity: %" PRIu64 " sectors "
                  "(%" PRIi64 " bytes)",
                  info->capacity, size);
    nbdkit_debug ("disk info: biosGeo: C:%" PRIu32 " H:%" PRIu32 " S:%" PRIu32,
                  info->biosGeo.cylinders,
                  info->biosGeo.heads,
                  info->biosGeo.sectors);
    nbdkit_debug ("disk info: physGeo: C:%" PRIu32 " H:%" PRIu32 " S:%" PRIu32,
                  info->physGeo.cylinders,
                  info->physGeo.heads,
                  info->physGeo.sectors);
    nbdkit_debug ("disk info: adapter type: %d",
                  (int) info->adapterType);
    nbdkit_debug ("disk info: num links: %d", info->numLinks);
    nbdkit_debug ("disk info: parent filename hint: %s",
                  info->parentFileNameHint ? : "NULL");
    nbdkit_debug ("disk info: uuid: %s",
                  info->uuid ? : "NULL");
    if (library_version >= 7) {
      nbdkit_debug ("disk info: sector size: "
                    "logical %" PRIu32 " physical %" PRIu32,
                    info->logicalSectorSize,
                    info->physicalSectorSize);
    }
  }

  VDDK_CALL_START (VixDiskLib_FreeInfo, "info")
    VixDiskLib_FreeInfo (info);
  VDDK_CALL_END (VixDiskLib_FreeInfo, 0);

  return (int64_t) size;
}

static int
do_read (struct command *cmd, struct vddk_handle *h)
{
  VixError err;
  uint32_t count = cmd->count;
  uint64_t offset = cmd->offset;
  void *buf = cmd->ptr;

  /* Align to sectors. */
  if (!IS_ALIGNED (offset, VIXDISKLIB_SECTOR_SIZE)) {
    nbdkit_error ("%s is not aligned to sectors", "read");
    return -1;
  }
  if (!IS_ALIGNED (count, VIXDISKLIB_SECTOR_SIZE)) {
    nbdkit_error ("%s is not aligned to sectors", "read");
    return -1;
  }
  offset /= VIXDISKLIB_SECTOR_SIZE;
  count /= VIXDISKLIB_SECTOR_SIZE;

  VDDK_CALL_START (VixDiskLib_ReadAsync,
                   "handle, %" PRIu64 " sectors, "
                   "%" PRIu32 " sectors, buffer, callback, %" PRIu64,
                   offset, count, cmd->id)
    err = VixDiskLib_ReadAsync (h->handle, offset, count, buf,
                                complete_command, cmd);
  VDDK_CALL_END (VixDiskLib_ReadAsync, count * VIXDISKLIB_SECTOR_SIZE);
  if (err != VIX_ASYNC) {
    VDDK_ERROR (err, "VixDiskLib_ReadAsync");
    return -1;
  }

  return 0;
}

static int
do_write (struct command *cmd, struct vddk_handle *h)
{
  VixError err;
  uint32_t count = cmd->count;
  uint64_t offset = cmd->offset;
  const void *buf = cmd->ptr;

  /* Align to sectors. */
  if (!IS_ALIGNED (offset, VIXDISKLIB_SECTOR_SIZE)) {
    nbdkit_error ("%s is not aligned to sectors", "write");
    return -1;
  }
  if (!IS_ALIGNED (count, VIXDISKLIB_SECTOR_SIZE)) {
    nbdkit_error ("%s is not aligned to sectors", "write");
    return -1;
  }
  offset /= VIXDISKLIB_SECTOR_SIZE;
  count /= VIXDISKLIB_SECTOR_SIZE;

  VDDK_CALL_START (VixDiskLib_WriteAsync,
                   "handle, %" PRIu64 " sectors, "
                   "%" PRIu32 " sectors, buffer, callback, %" PRIu64,
                   offset, count, cmd->id)
    err = VixDiskLib_WriteAsync (h->handle, offset, count, buf,
                                 complete_command, cmd);
  VDDK_CALL_END (VixDiskLib_WriteAsync, count * VIXDISKLIB_SECTOR_SIZE);
  if (err != VIX_ASYNC) {
    VDDK_ERROR (err, "VixDiskLib_WriteAsync");
    return -1;
  }

  return 0;
}

static int
do_flush (struct command *cmd, struct vddk_handle *h)
{
  VixError err;

  /* It seems safer to wait for outstanding asynchronous commands to
   * complete before doing a flush, so do this but ignore errors
   * except to print them.
   */
  VDDK_CALL_START (VixDiskLib_Wait, "handle")
    err = VixDiskLib_Wait (h->handle);
  VDDK_CALL_END (VixDiskLib_Wait, 0);
  if (err != VIX_OK)
    VDDK_ERROR (err, "VixDiskLib_Wait");

  /* The documentation for Flush is missing, but the comment in the
   * header file seems to indicate that it waits for WriteAsync
   * commands to finish.  There's a new function Wait to wait for
   * those.  However I verified using strace that in fact Flush calls
   * fsync on the file so it appears to be the correct call to use
   * here.
   */
  VDDK_CALL_START (VixDiskLib_Flush, "handle")
    err = VixDiskLib_Flush (h->handle);
  VDDK_CALL_END (VixDiskLib_Flush, 0);
  if (err != VIX_OK) {
    VDDK_ERROR (err, "VixDiskLib_Flush");
    return -1;
  }

  return 0;
}

static int
do_can_extents (struct command *cmd, struct vddk_handle *h)
{
  VixError err;
  VixDiskLibBlockList *block_list;

  /* This call was added in VDDK 6.7.  In earlier versions the
   * function pointer will be NULL and we cannot query extents.
   */
  if (VixDiskLib_QueryAllocatedBlocks == NULL) {
    nbdkit_debug ("can_extents: VixDiskLib_QueryAllocatedBlocks == NULL, "
                  "probably this is VDDK < 6.7");
    return 0;
  }

  /* Suppress errors around this call.  See:
   * https://bugzilla.redhat.com/show_bug.cgi?id=1709211#c7
   */
  error_suppression = 1;

  /* However even when the call is available it rarely works well so
   * the best thing we can do here is to try the call and if it's
   * non-functional return false.
   */
  VDDK_CALL_START (VixDiskLib_QueryAllocatedBlocks,
                   "handle, 0, %d sectors, %d sectors",
                   VIXDISKLIB_MIN_CHUNK_SIZE, VIXDISKLIB_MIN_CHUNK_SIZE)
    err = VixDiskLib_QueryAllocatedBlocks (h->handle,
                                           0, VIXDISKLIB_MIN_CHUNK_SIZE,
                                           VIXDISKLIB_MIN_CHUNK_SIZE,
                                           &block_list);
  VDDK_CALL_END (VixDiskLib_QueryAllocatedBlocks, 0);
  error_suppression = 0;
  if (err == VIX_OK) {
    VDDK_CALL_START (VixDiskLib_FreeBlockList, "block_list")
      VixDiskLib_FreeBlockList (block_list);
    VDDK_CALL_END (VixDiskLib_FreeBlockList, 0);
  }
  if (err != VIX_OK) {
    char *errmsg = VixDiskLib_GetErrorText (err, NULL);
    nbdkit_debug ("can_extents: "
                  "VixDiskLib_QueryAllocatedBlocks test failed, "
                  "extents support will be disabled: "
                  "original error: %s",
                  errmsg);
    VixDiskLib_FreeErrorText (errmsg);
    return 0;
  }

  return 1;
}

/* Add an extent to the list of extents. */
static int
add_extent (struct nbdkit_extents *extents,
            uint64_t *position, uint64_t next_position, bool is_hole)
{
  uint32_t type = 0;
  const uint64_t length = next_position - *position;

  if (is_hole) {
    type = NBDKIT_EXTENT_HOLE;
    /* Images opened as single link might be backed by another file in the
       chain, so the holes are not guaranteed to be zeroes. */
    if (!single_link)
      type |= NBDKIT_EXTENT_ZERO;
  }

  assert (*position <= next_position);
  if (*position == next_position)
    return 0;

  if (vddk_debug_extents)
    nbdkit_debug ("adding extent type %s at [%" PRIu64 "...%" PRIu64 "]",
                  is_hole ? "hole" : "allocated data",
                  *position, next_position-1);
  if (nbdkit_add_extent (extents, *position, length, type) == -1)
    return -1;

  *position = next_position;
  return 0;
}

static int
do_extents (struct command *cmd, struct vddk_handle *h)
{
  uint32_t count = cmd->count;
  uint64_t offset = cmd->offset;
  bool req_one = cmd->req_one;
  struct nbdkit_extents *extents = cmd->ptr;
  uint64_t position, end, start_sector;

  position = offset;
  end = offset + count;

  /* We can only query whole chunks.  Therefore start with the
   * first chunk before offset.
   */
  start_sector =
    ROUND_DOWN (offset, VIXDISKLIB_MIN_CHUNK_SIZE * VIXDISKLIB_SECTOR_SIZE)
    / VIXDISKLIB_SECTOR_SIZE;
  while (start_sector * VIXDISKLIB_SECTOR_SIZE < end) {
    VixError err;
    uint32_t i;
    uint64_t nr_chunks, nr_sectors;
    VixDiskLibBlockList *block_list;

    assert (IS_ALIGNED (start_sector, VIXDISKLIB_MIN_CHUNK_SIZE));

    nr_chunks =
      ROUND_UP (end - start_sector * VIXDISKLIB_SECTOR_SIZE,
                VIXDISKLIB_MIN_CHUNK_SIZE * VIXDISKLIB_SECTOR_SIZE)
      / (VIXDISKLIB_MIN_CHUNK_SIZE * VIXDISKLIB_SECTOR_SIZE);
    nr_chunks = MIN (nr_chunks, VIXDISKLIB_MAX_CHUNK_NUMBER);
    nr_sectors = nr_chunks * VIXDISKLIB_MIN_CHUNK_SIZE;

    VDDK_CALL_START (VixDiskLib_QueryAllocatedBlocks,
                     "handle, %" PRIu64 " sectors, %" PRIu64 " sectors, "
                     "%d sectors",
                     start_sector, nr_sectors, VIXDISKLIB_MIN_CHUNK_SIZE)
      err = VixDiskLib_QueryAllocatedBlocks (h->handle,
                                             start_sector, nr_sectors,
                                             VIXDISKLIB_MIN_CHUNK_SIZE,
                                             &block_list);
    VDDK_CALL_END (VixDiskLib_QueryAllocatedBlocks, 0);
    if (err != VIX_OK) {
      VDDK_ERROR (err, "VixDiskLib_QueryAllocatedBlocks");
      return -1;
    }

    for (i = 0; i < block_list->numBlocks; ++i) {
      uint64_t blk_offset, blk_length;

      blk_offset = block_list->blocks[i].offset * VIXDISKLIB_SECTOR_SIZE;
      blk_length = block_list->blocks[i].length * VIXDISKLIB_SECTOR_SIZE;

      /* The query returns allocated blocks.  We must insert holes
       * between the blocks as necessary.
       */
      if ((position < blk_offset &&
           add_extent (extents, &position, blk_offset, true) == -1) ||
          (add_extent (extents,
                       &position, blk_offset + blk_length, false) == -1)) {
        VDDK_CALL_START (VixDiskLib_FreeBlockList, "block_list")
          VixDiskLib_FreeBlockList (block_list);
        VDDK_CALL_END (VixDiskLib_FreeBlockList, 0);
        return -1;
      }
    }
    VDDK_CALL_START (VixDiskLib_FreeBlockList, "block_list")
      VixDiskLib_FreeBlockList (block_list);
    VDDK_CALL_END (VixDiskLib_FreeBlockList, 0);

    /* There's an implicit hole after the returned list of blocks,
     * up to the end of the QueryAllocatedBlocks request.
     */
    if (add_extent (extents,
                    &position,
                    (start_sector + nr_sectors) * VIXDISKLIB_SECTOR_SIZE,
                    true) == -1) {
      return -1;
    }

    start_sector += nr_sectors;

    /* If one extent was requested, as long as we've added an extent
     * overlapping the original offset we're done.
     */
    if (req_one && position > offset)
      break;
  }

  return 0;
}

/* Background worker thread, one per connection, which is where the
 * VDDK commands are issued.
 */
void *
vddk_worker_thread (void *handle)
{
  struct vddk_handle *h = handle;
  bool stop = false;

  while (!stop) {
    struct command *cmd;
    int r;
    bool async = false;

    /* Wait until we are sent at least one command. */
    {
      ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&h->commands_lock);
      while (h->commands.size == 0)
        pthread_cond_wait (&h->commands_cond, &h->commands_lock);
      cmd = h->commands.ptr[0];
      command_queue_remove (&h->commands, 0);
    }

    switch (cmd->type) {
    case STOP:
      r = do_stop (cmd, h);
      stop = true;
      break;

    case GET_SIZE: {
      int64_t size = do_get_size (cmd, h);
      if (size == -1)
        r = -1;
      else {
        r = 0;
        *(uint64_t *)cmd->ptr = size;
      }
      break;
    }

    case READ:
      r = do_read (cmd, h);
      /* If async is true, don't retire this command now. */
      async = r == 0;
      break;

    case WRITE:
      r = do_write (cmd, h);
      /* If async is true, don't retire this command now. */
      async = r == 0;
      break;

    case FLUSH:
      r = do_flush (cmd, h);
      break;

    case CAN_EXTENTS:
      r = do_can_extents (cmd, h);
      if (r >= 0)
        *(int *)cmd->ptr = r;
      break;

    case EXTENTS:
      r = do_extents (cmd, h);
      break;

    default: abort (); /* impossible, but keeps GCC happy */
    } /* switch */

    if (!async) {
      /* Update the command status. */
      ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&cmd->mutex);
      cmd->status = r >= 0 ? SUCCEEDED : FAILED;

      /* For synchronous commands signal the caller thread that the
       * command has completed.  (Asynchronous commands are completed in
       * the callback handler).
       */
      pthread_cond_signal (&cmd->cond);
    }
  } /* while (!stop) */

  /* Exit the worker thread. */
  return NULL;
}
