/* nbdkit
 * Copyright (C) 2018 Red Hat Inc.
 * All rights reserved.
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
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <assert.h>

#include <pthread.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <nbdkit-filter.h>

#include "cache.h"
#include "blk.h"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* In order to handle parallel requests safely, this lock must be held
 * when calling any blk_* functions.
 */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

enum cache_mode cache_mode = CACHE_MODE_WRITEBACK;
bool cache_on_read = false;

static int cache_flush (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle, uint32_t flags, int *err);

static void
cache_load (void)
{
  if (blk_init () == -1)
    exit (EXIT_FAILURE);
}

static void
cache_unload (void)
{
  blk_free ();
}

static int
cache_config (nbdkit_next_config *next, void *nxdata,
              const char *key, const char *value)
{
  if (strcmp (key, "cache") == 0) {
    if (strcmp (value, "writeback") == 0) {
      cache_mode = CACHE_MODE_WRITEBACK;
      return 0;
    }
    else if (strcmp (value, "writethrough") == 0) {
      cache_mode = CACHE_MODE_WRITETHROUGH;
      return 0;
    }
    else if (strcmp (value, "unsafe") == 0) {
      cache_mode = CACHE_MODE_UNSAFE;
      return 0;
    }
    else {
      nbdkit_error ("invalid cache parameter, should be "
                    "writeback|writethrough|unsafe");
      return -1;
    }
  }
  else if (strcmp (key, "cache-on-read") == 0) {
    int r;

    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    cache_on_read = r;
    return 0;
  }
  else {
    return next (nxdata, key, value);
  }
}

static void *
cache_open (nbdkit_next_open *next, void *nxdata, int readonly)
{
  /* We don't use the handle, so this just provides a non-NULL
   * pointer that we can return.
   */
  static int handle;

  if (next (nxdata, readonly) == -1)
    return NULL;

  return &handle;
}

/* Get the file size and ensure the cache is the correct size. */
static int64_t
cache_get_size (struct nbdkit_next_ops *next_ops, void *nxdata,
              void *handle)
{
  int64_t size;
  int r;

  size = next_ops->get_size (nxdata);
  if (size == -1)
    return -1;

  nbdkit_debug ("cache: underlying file size: %" PRIi64, size);

  pthread_mutex_lock (&lock);
  r = blk_set_size (size);
  pthread_mutex_unlock (&lock);
  if (r == -1)
    return -1;

  return size;
}

/* Force an early call to cache_get_size, consequently truncating the
 * cache to the correct size.
 */
static int
cache_prepare (struct nbdkit_next_ops *next_ops, void *nxdata,
               void *handle)
{
  int64_t r;

  r = cache_get_size (next_ops, nxdata, handle);
  if (r < 0)
    return -1;
  /* TODO: cache per-connection FUA mode? */
  return 0;
}

/* Read data. */
static int
cache_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle, void *buf, uint32_t count, uint64_t offset,
             uint32_t flags, int *err)
{
  uint8_t *block;

  assert (!flags);
  block = malloc (BLKSIZE);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  while (count > 0) {
    uint64_t blknum, blkoffs, n;
    int r;

    blknum = offset / BLKSIZE;  /* block number */
    blkoffs = offset % BLKSIZE; /* offset within the block */
    n = BLKSIZE - blkoffs;      /* max bytes we can read from this block */
    if (n > count)
      n = count;

    pthread_mutex_lock (&lock);
    r = blk_read (next_ops, nxdata, blknum, block, err);
    pthread_mutex_unlock (&lock);
    if (r == -1) {
      free (block);
      return -1;
    }

    memcpy (buf, &block[blkoffs], n);

    buf += n;
    count -= n;
    offset += n;
  }

  free (block);
  return 0;
}

/* Write data. */
static int
cache_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
              void *handle, const void *buf, uint32_t count, uint64_t offset,
              uint32_t flags, int *err)
{
  uint8_t *block;
  bool need_flush = false;

  block = malloc (BLKSIZE);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  if ((flags & NBDKIT_FLAG_FUA) &&
      next_ops->can_fua (nxdata) == NBDKIT_FUA_EMULATE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }
  while (count > 0) {
    uint64_t blknum, blkoffs, n;
    int r;

    blknum = offset / BLKSIZE;  /* block number */
    blkoffs = offset % BLKSIZE; /* offset within the block */
    n = BLKSIZE - blkoffs;      /* max bytes we can read from this block */
    if (n > count)
      n = count;

    /* Do a read-modify-write operation on the current block.
     * Hold the lock over the whole operation.
     */
    pthread_mutex_lock (&lock);
    r = blk_read (next_ops, nxdata, blknum, block, err);
    if (r != -1) {
      memcpy (&block[blkoffs], buf, n);
      r = blk_write (next_ops, nxdata, blknum, block, flags, err);
    }
    pthread_mutex_unlock (&lock);
    if (r == -1) {
      free (block);
      return -1;
    }

    buf += n;
    count -= n;
    offset += n;
  }

  free (block);
  if (need_flush)
    return cache_flush (next_ops, nxdata, handle, 0, err);
  return 0;
}

/* Zero data. */
static int
cache_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle, uint32_t count, uint64_t offset, uint32_t flags,
            int *err)
{
  uint8_t *block;
  bool need_flush = false;

  block = malloc (BLKSIZE);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  flags &= ~NBDKIT_FLAG_MAY_TRIM; /* See BLKSIZE comment above. */
  if ((flags & NBDKIT_FLAG_FUA) &&
      next_ops->can_fua (nxdata) == NBDKIT_FUA_EMULATE) {
    flags &= ~NBDKIT_FLAG_FUA;
    need_flush = true;
  }
  while (count > 0) {
    uint64_t blknum, blkoffs, n;
    int r;

    blknum = offset / BLKSIZE;  /* block number */
    blkoffs = offset % BLKSIZE; /* offset within the block */
    n = BLKSIZE - blkoffs;      /* max bytes we can read from this block */
    if (n > count)
      n = count;

    /* Do a read-modify-write operation on the current block.
     * Hold the lock over the whole operation.
     */
    pthread_mutex_lock (&lock);
    r = blk_read (next_ops, nxdata, blknum, block, err);
    if (r != -1) {
      memset (&block[blkoffs], 0, n);
      r = blk_write (next_ops, nxdata, blknum, block, flags, err);
    }
    pthread_mutex_unlock (&lock);
    if (r == -1) {
      free (block);
      return -1;
    }

    count -= n;
    offset += n;
  }

  free (block);
  if (need_flush)
    return cache_flush (next_ops, nxdata, handle, 0, err);
  return 0;
}

/* Flush: Go through all the dirty blocks, flushing them to disk. */
struct flush_data {
  uint8_t *block;               /* bounce buffer */
  unsigned errors;              /* count of errors seen */
  int first_errno;              /* first errno seen */
  struct nbdkit_next_ops *next_ops;
  void *nxdata;
};

static int flush_dirty_block (uint64_t blknum, void *);

static int
cache_flush (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle,
             uint32_t flags, int *err)
{
  struct flush_data data =
    { .errors = 0, .first_errno = 0, .next_ops = next_ops, .nxdata = nxdata };
  int tmp;

  if (cache_mode == CACHE_MODE_UNSAFE)
    return 0;

  assert (!flags);

  /* Allocate the bounce buffer. */
  data.block = malloc (BLKSIZE);
  if (data.block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  /* In theory if cache_mode == CACHE_MODE_WRITETHROUGH then there
   * should be no dirty blocks.  However we go through the cache here
   * to be sure.  Also we still need to issue the flush to the
   * underlying storage.
   */
  pthread_mutex_lock (&lock);
  for_each_dirty_block (flush_dirty_block, &data);
  pthread_mutex_unlock (&lock);
  free (data.block);

  /* Now issue a flush request to the underlying storage. */
  if (next_ops->flush (nxdata, 0,
                       data.errors ? &tmp : &data.first_errno) == -1)
    data.errors++;

  if (data.errors > 0) {
    *err = data.first_errno;
    return -1;
  }
  return 0;
}

static int
flush_dirty_block (uint64_t blknum, void *datav)
{
  struct flush_data *data = datav;
  int tmp;

  /* Perform a read + writethrough which will read from the
   * cache and write it through to the underlying storage.
   */
  if (blk_read (data->next_ops, data->nxdata, blknum, data->block,
                data->errors ? &tmp : &data->first_errno) == -1)
    goto err;
  if (blk_writethrough (data->next_ops, data->nxdata, blknum, data->block, 0,
                        data->errors ? &tmp : &data->first_errno) == -1)
    goto err;

  return 0;

 err:
  nbdkit_error ("cache: flush of block %" PRIu64 " failed", blknum);
  data->errors++;
  return 0; /* continue scanning and flushing. */
}

static struct nbdkit_filter filter = {
  .name              = "cache",
  .longname          = "nbdkit caching filter",
  .version           = PACKAGE_VERSION,
  .load              = cache_load,
  .unload            = cache_unload,
  .config            = cache_config,
  .open              = cache_open,
  .prepare           = cache_prepare,
  .get_size          = cache_get_size,
  .pread             = cache_pread,
  .pwrite            = cache_pwrite,
  .zero              = cache_zero,
  .flush             = cache_flush,
};

NBDKIT_REGISTER_FILTER(filter)
