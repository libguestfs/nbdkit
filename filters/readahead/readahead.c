/* nbdkit
 * Copyright (C) 2019-2021 Red Hat Inc.
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
#include <errno.h>

#include <pthread.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "minmax.h"

/* Copied from server/plugins.c. */
#define MAX_REQUEST_SIZE (64 * 1024 * 1024)

/* These could be made configurable in future. */
#define READAHEAD_MIN 65536
#define READAHEAD_MAX MAX_REQUEST_SIZE

/* This lock protects the global state. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* The real size of the underlying plugin. */
static uint64_t size;

/* Size of the readahead window. */
static uint64_t window = READAHEAD_MIN;

/* The single prefetch buffer shared by all threads, and its virtual
 * location in the virtual disk.  The prefetch buffer grows
 * dynamically as required, but never shrinks.
 */
static char *buffer = NULL;
static size_t bufsize = 0;
static uint64_t position;
static uint32_t length = 0;

static void
readahead_unload (void)
{
  free (buffer);
}

static int64_t readahead_get_size (nbdkit_next *next, void *handle);

/* In prepare, force a call to get_size which sets the size global. */
static int
readahead_prepare (nbdkit_next *next, void *handle, int readonly)
{
  int64_t r;

  r = readahead_get_size (next, handle);
  return r >= 0 ? 0 : -1;
}

/* Get the size. */
static int64_t
readahead_get_size (nbdkit_next *next, void *handle)
{
  int64_t r;

  r = next->get_size (next);
  if (r == -1)
    return -1;

  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
  size = r;

  return r;
}

/* Cache */
static int
readahead_can_cache (nbdkit_next *next, void *handle)
{
  /* We are already operating as a cache regardless of the plugin's
   * underlying .can_cache, but it's easiest to just rely on nbdkit's
   * behavior of calling .pread for caching.
   */
  return NBDKIT_CACHE_EMULATE;
}

/* Read data. */

static int
fill_readahead (nbdkit_next *next,
                uint32_t count, uint64_t offset, uint32_t flags, int *err)
{
  position = offset;

  /* Read at least window bytes, but if count is larger read that.
   * Note that the count cannot be bigger than the buffer size.
   */
  length = MAX (count, window);

  /* Don't go beyond the end of the underlying file. */
  length = MIN (length, size - position);

  /* Grow the buffer if necessary. */
  if (bufsize < length) {
    char *new_buffer = realloc (buffer, length);
    if (new_buffer == NULL) {
      *err = errno;
      nbdkit_error ("realloc: %m");
      return -1;
    }
    buffer = new_buffer;
    bufsize = length;
  }

  if (next->pread (next, buffer, length, offset, flags, err) == -1) {
    length = 0;           /* failed to fill the prefetch buffer */
    return -1;
  }

  return 0;
}

static int
readahead_pread (nbdkit_next *next,
                 void *handle, void *buf, uint32_t count, uint64_t offset,
                 uint32_t flags, int *err)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);

  while (count > 0) {
    if (length == 0) {
      /* We don't have a prefetch buffer at all.  This could be the
       * first request or reset after a miss.
       */
      window = READAHEAD_MIN;
      if (fill_readahead (next, count, offset, flags, err) == -1)
        return -1;
    }

    /* Can we satisfy this request partly or entirely from the prefetch
     * buffer?
     */
    else if (position <= offset && offset < position + length) {
      uint32_t n = MIN (position - offset + length, count);
      memcpy (buf, &buffer[offset-position], n);
      buf += n;
      offset += n;
      count -= n;
    }

    /* Does the request start immediately after the prefetch buffer?
     * This is a “hit” allowing us to double the window size.
     */
    else if (offset == position + length) {
      window = MIN (window * 2, READAHEAD_MAX);
      if (fill_readahead (next, count, offset, flags, err) == -1)
        return -1;
    }

    /* Else it's a “miss”.  Reset everything and start again. */
    else
      length = 0;
  }

  return 0;
}

/* Any writes or write-like operations kill the prefetch buffer.
 *
 * We could do better here, but for the current use case of this
 * filter it doesn't matter. XXX
 */

static void
kill_readahead (void)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
  window = READAHEAD_MIN;
  length = 0;
}

static int
readahead_pwrite (nbdkit_next *next,
                  void *handle,
                  const void *buf, uint32_t count, uint64_t offset,
                  uint32_t flags, int *err)
{
  kill_readahead ();
  return next->pwrite (next, buf, count, offset, flags, err);
}

static int
readahead_trim (nbdkit_next *next,
                void *handle,
                uint32_t count, uint64_t offset, uint32_t flags,
                int *err)
{
  kill_readahead ();
  return next->trim (next, count, offset, flags, err);
}

static int
readahead_zero (nbdkit_next *next,
                void *handle,
                uint32_t count, uint64_t offset, uint32_t flags,
                int *err)
{
  kill_readahead ();
  return next->zero (next, count, offset, flags, err);
}

static int
readahead_flush (nbdkit_next *next,
                 void *handle, uint32_t flags, int *err)
{
  kill_readahead ();
  return next->flush (next, flags, err);
}

static struct nbdkit_filter filter = {
  .name              = "readahead",
  .longname          = "nbdkit readahead filter",
  .unload            = readahead_unload,
  .prepare           = readahead_prepare,
  .get_size          = readahead_get_size,
  .can_cache         = readahead_can_cache,
  .pread             = readahead_pread,
  .pwrite            = readahead_pwrite,
  .trim              = readahead_trim,
  .zero              = readahead_zero,
  .flush             = readahead_flush,
};

NBDKIT_REGISTER_FILTER(filter)
