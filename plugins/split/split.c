/* nbdkit
 * Copyright (C) 2017-2018 Red Hat Inc.
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

#include <nbdkit-plugin.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

/* The files. */
static char **filenames = NULL;
static size_t nr_files = 0;

static void
split_unload (void)
{
  size_t i;

  for (i = 0; i < nr_files; ++i)
    free (filenames[i]);
  free (filenames);
}

static int
split_config (const char *key, const char *value)
{
  char **new_filenames;

  if (strcmp (key, "file") == 0) {
    new_filenames = realloc (filenames, (nr_files+1) * sizeof (char *));
    if (new_filenames == NULL) {
      nbdkit_error ("malloc: %m");
      return -1;
    }
    filenames = new_filenames;
    filenames[nr_files] = nbdkit_realpath (value);
    if (filenames[nr_files] == NULL)
      return -1;
    nr_files++;
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

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  h->files = malloc (nr_files * sizeof (struct file));
  if (h->files == NULL) {
    nbdkit_error ("malloc: %m");
    free (h);
    return NULL;
  }
  for (i = 0; i < nr_files; ++i)
    h->files[i].fd = -1;

  /* Open the files. */
  flags = O_CLOEXEC|O_NOCTTY;
  if (readonly)
    flags |= O_RDONLY;
  else
    flags |= O_RDWR;

  for (i = 0; i < nr_files; ++i) {
    h->files[i].fd = open (filenames[i], flags);
    if (h->files[i].fd == -1) {
      nbdkit_error ("open: %s: %m", filenames[i]);
      goto err;
    }
  }

  offset = 0;
  for (i = 0; i < nr_files; ++i) {
    h->files[i].offset = offset;

    if (fstat (h->files[i].fd, &statbuf) == -1) {
      nbdkit_error ("stat: %s: %m", filenames[i]);
      goto err;
    }
    h->files[i].size = statbuf.st_size;
    offset += statbuf.st_size;

    nbdkit_debug ("file[%zu]=%s: offset=%" PRIu64 ", size=%" PRIu64,
                  i, filenames[i], h->files[i].offset, h->files[i].size);
  }
  h->size = offset;
  nbdkit_debug ("total size=%" PRIu64, h->size);

  return h;

 err:
  for (i = 0; i < nr_files; ++i) {
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

  for (i = 0; i < nr_files; ++i)
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
                  nr_files, sizeof (struct file),
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
  .pread             = split_pread,
  .pwrite            = split_pwrite,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
