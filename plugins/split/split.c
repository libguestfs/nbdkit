/* nbdkit
 * Copyright (C) 2017-2020 Red Hat Inc.
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
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>

#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "pread.h"
#include "pwrite.h"
#include "windows-compat.h"
#include "vector.h"

/* The files. */
DEFINE_VECTOR_TYPE(string_vector, char *);
static string_vector filenames = empty_vector;

/* Any callbacks using lseek must be protected by this lock. */
static pthread_mutex_t lseek_lock = PTHREAD_MUTEX_INITIALIZER;

static void
split_unload (void)
{
  string_vector_iter (&filenames, (void *) free);
  free (filenames.ptr);
}

static int
split_config (const char *key, const char *value)
{
  char *s;

  if (strcmp (key, "file") == 0) {
    s = nbdkit_realpath (value);
    if (s == NULL)
      return -1;
    if (string_vector_append (&filenames, s) == -1) {
      nbdkit_error ("realloc: %m");
      return -1;
    }
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

#define split_config_help \
  "file=<FILENAME>  (required) File(s) to serve."

/* The per-connection handle. */
struct handle {
  struct file *files;
  uint64_t size;                /* Total concatenated size. */
};

struct file {
  uint64_t offset, size;
  int fd;
  bool can_extents;
};

/* Create the per-connection handle. */
static void *
split_open (int readonly)
{
  struct handle *h;
  int flags;
  size_t i;
  uint64_t offset;
  struct stat statbuf;
  off_t r;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  h->files = malloc (filenames.size * sizeof (struct file));
  if (h->files == NULL) {
    nbdkit_error ("malloc: %m");
    free (h);
    return NULL;
  }
  for (i = 0; i < filenames.size; ++i)
    h->files[i].fd = -1;

  /* Open the files. */
  flags = O_CLOEXEC|O_NOCTTY;
  if (readonly)
    flags |= O_RDONLY;
  else
    flags |= O_RDWR;

  for (i = 0; i < filenames.size; ++i) {
    h->files[i].fd = open (filenames.ptr[i], flags);
    if (h->files[i].fd == -1) {
      nbdkit_error ("open: %s: %m", filenames.ptr[i]);
      goto err;
    }
  }

  offset = 0;
  for (i = 0; i < filenames.size; ++i) {
    h->files[i].offset = offset;

    if (fstat (h->files[i].fd, &statbuf) == -1) {
      nbdkit_error ("stat: %s: %m", filenames.ptr[i]);
      goto err;
    }
    h->files[i].size = statbuf.st_size;
    offset += statbuf.st_size;

    nbdkit_debug ("file[%zu]=%s: offset=%" PRIu64 ", size=%" PRIu64,
                  i, filenames.ptr[i], h->files[i].offset, h->files[i].size);

#ifdef SEEK_HOLE
    /* Test if this file supports extents. */
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lseek_lock);
    r = lseek (h->files[i].fd, 0, SEEK_DATA);
    if (r == -1 && errno != ENXIO) {
      nbdkit_debug ("disabling extents: lseek on %s: %m", filenames.ptr[i]);
      h->files[i].can_extents = false;
    }
    else
      h->files[i].can_extents = true;
#else
    h->files[i].can_extents = false;
#endif
  }
  h->size = offset;
  nbdkit_debug ("total size=%" PRIu64, h->size);

  return h;

 err:
  for (i = 0; i < filenames.size; ++i) {
    if (h->files[i].fd >= 0)
      close (h->files[i].fd);
  }
  free (h->files);
  free (h);
  return NULL;
}

/* Free up the per-connection handle. */
static void
split_close (void *handle)
{
  struct handle *h = handle;
  size_t i;

  for (i = 0; i < filenames.size; ++i)
    close (h->files[i].fd);
  free (h->files);
  free (h);
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_REQUESTS

/* Get the disk size. */
static int64_t
split_get_size (void *handle)
{
  struct handle *h = handle;

  return (int64_t) h->size;
}

static int
split_can_cache (void *handle)
{
  /* Prefer posix_fadvise(), but letting nbdkit call .pread on our
   * behalf also tends to work well for the local file system
   * cache.
   */
#if HAVE_POSIX_FADVISE
  return NBDKIT_FUA_NATIVE;
#else
  return NBDKIT_FUA_EMULATE;
#endif
}

/* Helper function to map the offset to the correct file. */
static int
compare_offset (const void *offsetp, const void *filep)
{
  const uint64_t offset = *(uint64_t *)offsetp;
  const struct file *file = (struct file *)filep;

  if (offset < file->offset) return -1;
  if (offset >= file->offset + file->size) return 1;
  return 0;
}

static struct file *
get_file (struct handle *h, uint64_t offset)
{
  return bsearch (&offset, h->files,
                  filenames.size, sizeof (struct file),
                  compare_offset);
}

/* Read data. */
static int
split_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  struct handle *h = handle;

  while (count > 0) {
    struct file *file = get_file (h, offset);
    uint64_t foffs = offset - file->offset;
    uint64_t max;
    ssize_t r;

    max = file->size - foffs;
    if (max > count)
      max = count;

    r = pread (file->fd, buf, max, foffs);
    if (r == -1) {
      nbdkit_error ("pread: %m");
      return -1;
    }
    if (r == 0) {
      nbdkit_error ("pread: unexpected end of file");
      return -1;
    }
    buf += r;
    count -= r;
    offset += r;
  }

  return 0;
}

/* Write data to the file. */
static int
split_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset)
{
  struct handle *h = handle;

  while (count > 0) {
    struct file *file = get_file (h, offset);
    uint64_t foffs = offset - file->offset;
    uint64_t max;
    ssize_t r;

    max = file->size - foffs;
    if (max > count)
      max = count;

    r = pwrite (file->fd, buf, max, offset);
    if (r == -1) {
      nbdkit_error ("pwrite: %m");
      return -1;
    }
    buf += r;
    count -= r;
    offset += r;
  }

  return 0;
}

#if HAVE_POSIX_FADVISE
/* Caching. */
static int
split_cache (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  struct handle *h = handle;

  /* Cache is advisory, we don't care if this fails */
  while (count > 0) {
    struct file *file = get_file (h, offset);
    uint64_t foffs = offset - file->offset;
    uint64_t max;
    int r;

    max = file->size - foffs;
    if (max > count)
      max = count;

    r = posix_fadvise (file->fd, offset, max, POSIX_FADV_WILLNEED);
    if (r) {
      errno = r;
      nbdkit_error ("posix_fadvise: %m");
      return -1;
    }
    count -= r;
    offset += r;
  }

  return 0;
}
#endif /* HAVE_POSIX_FADVISE */

#ifdef SEEK_HOLE
static int64_t
do_extents (struct file *file, uint32_t count, uint64_t offset,
            bool req_one, struct nbdkit_extents *extents)
{
  int64_t r = 0;
  uint64_t end = offset + count;

  do {
    off_t pos;

    pos = lseek (file->fd, offset, SEEK_DATA);
    if (pos == -1) {
      if (errno == ENXIO) {
        /* The current man page does not describe this situation well,
         * but a proposed change to POSIX adds these words for ENXIO:
         * "or the whence argument is SEEK_DATA and the offset falls
         * within the final hole of the file."
         */
        pos = end;
      }
      else {
        nbdkit_error ("lseek: SEEK_DATA: %" PRIu64 ": %m", offset);
        return -1;
      }
    }

    /* We know there is a hole from offset to pos-1. */
    if (pos > offset) {
      if (nbdkit_add_extent (extents, offset + file->offset, pos - offset,
                             NBDKIT_EXTENT_HOLE | NBDKIT_EXTENT_ZERO) == -1)
        return -1;
      r += pos - offset;
      if (req_one)
        break;
    }

    offset = pos;
    if (offset >= end)
      break;

    pos = lseek (file->fd, offset, SEEK_HOLE);
    if (pos == -1) {
      nbdkit_error ("lseek: SEEK_HOLE: %" PRIu64 ": %m", offset);
      return -1;
    }

    /* We know there is data from offset to pos-1. */
    if (pos > offset) {
      if (nbdkit_add_extent (extents, offset + file->offset, pos - offset,
                             0 /* allocated data */) == -1)
        return -1;
      r += pos - offset;
      if (req_one)
        break;
    }

    offset = pos;
  } while (offset < end);

  return r;
}

static int
split_extents (void *handle, uint32_t count, uint64_t offset,
               uint32_t flags, struct nbdkit_extents *extents)
{
  struct handle *h = handle;
  const bool req_one = flags & NBDKIT_FLAG_REQ_ONE;

  while (count > 0) {
    struct file *file = get_file (h, offset);
    uint64_t foffs = offset - file->offset;
    uint64_t max;
    int64_t r;

    max = file->size - foffs;
    if (max > count)
      max = count;

    if (file->can_extents) {
      ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lseek_lock);
      max = r = do_extents (file, max, foffs, req_one, extents);
    }
    else
      r = nbdkit_add_extent (extents, offset, max, 0 /* allocated data */);
    if (r == -1)
      return -1;
    count -= max;
    offset += max;
    if (req_one)
      break;
  }

  return 0;
}
#endif /* SEEK_HOLE */

static struct nbdkit_plugin plugin = {
  .name              = "split",
  .version           = PACKAGE_VERSION,
  .unload            = split_unload,
  .config            = split_config,
  .config_help       = split_config_help,
  .magic_config_key  = "file",
  .open              = split_open,
  .close             = split_close,
  .get_size          = split_get_size,
  .can_cache         = split_can_cache,
  .pread             = split_pread,
  .pwrite            = split_pwrite,
#if HAVE_POSIX_FADVISE
  .cache             = split_cache,
#endif
#ifdef SEEK_HOLE
  .extents           = split_extents,
#endif
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
