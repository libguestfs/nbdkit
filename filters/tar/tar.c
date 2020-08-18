/* nbdkit
 * Copyright (C) 2018-2020 Red Hat Inc.
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
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <pthread.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "poll.h"
#include "minmax.h"
#include "utils.h"

static const char *entry;       /* File within tar (tar-entry=...) */
static const char *tar_program = "tar";

/* Offset and size within tarball.
 *
 * These are calculated once in the first connection that calls
 * tar_prepare.  They are protected by the lock.
 */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static bool initialized = false;
static uint64_t tar_offset, tar_size;

static int
tar_config (nbdkit_next_config *next, void *nxdata,
            const char *key, const char *value)
{
  if (strcmp (key, "tar-entry") == 0) {
    if (entry) {
      nbdkit_error ("only one tar-entry parameter can be given");
      return -1;
    }
    entry = value;
    return 0;
  }
  else if (strcmp (key, "tar") == 0) {
    tar_program = value;
    return 0;
  }

  return next (nxdata, key, value);
}

static int
tar_config_complete (nbdkit_next_config_complete *next, void *nxdata)
{
  if (entry == NULL) {
    nbdkit_error ("you must supply the tar-entry=<FILENAME> parameter");
    return -1;
  }

  return next (nxdata);
}

#define tar_config_help \
  "tar-entry=<FILENAME> (required) The path inside the tar file to serve."

static int
tar_thread_model (void)
{
  return NBDKIT_THREAD_MODEL_PARALLEL;
}

struct handle {
  /* These are copied from the globals during tar_prepare, so that we
   * don't have to keep grabbing the lock on each request.
   */
  uint64_t offset, size;
};

static void *
tar_open (nbdkit_next_open *next, nbdkit_backend *nxdata,
          int readonly, const char *exportname, int is_tls)
{
  struct handle *h;

  if (next (nxdata, readonly, exportname) == -1)
    return NULL;

  h = calloc (1, sizeof *h);
  if (h == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }
  return h;
}

static void
tar_close (void *handle)
{
  free (handle);
}

/* Calculate the offset of the entry within the tarball.  This is
 * called with the lock held.  The method used is described here:
 * https://www.redhat.com/archives/libguestfs/2020-July/msg00017.html
 */
static int
calculate_offset_of_entry (struct nbdkit_next_ops *next_ops, void *nxdata)
{
  const size_t bufsize = 65536;
  char output[] = "/tmp/tarXXXXXX";
  int fd;
  FILE *fp;
  CLEANUP_FREE char *cmd = NULL;
  size_t cmdlen = 0;
  CLEANUP_FREE char *buf = NULL;
  int64_t i, copysize;
  bool scanned_ok = false;

  assert (entry);

  /* Temporary file to capture the output from the tar command. */
  fd = mkstemp (output);
  if (fd == -1) {
    nbdkit_error ("mkstemp: %m");
    return -1;
  }
  close (fd);

  /* Construct the tar command to examine the tar file. */
  fp = open_memstream (&cmd, &cmdlen);
  if (fp == NULL) {
    nbdkit_error ("open_memstream: %m");
    return -1;
  }
  fprintf (fp, "LANG=C ");
  shell_quote (tar_program, fp);
  fprintf (fp, " --no-auto-compress -t --block-number -v -f - ");
  shell_quote (entry, fp);
  fprintf (fp, " > ");
  shell_quote (output, fp);
  /* Unfortunately we have to hide stderr since we are
   * expecting tar to warn:
   *   tar: Unexpected EOF in archive
   *   tar: Error is not recoverable: exiting now
   * when we close the connection abruptly.
   */
  fprintf (fp, " 2>/dev/null");
  if (fclose (fp) == EOF) {
    nbdkit_error ("memstream failed: %m");
    return -1;
  }

  /* Prepare the copy buffer and copy size. */
  buf = malloc (bufsize);
  if (buf == NULL) {
    nbdkit_error ("malloc: %m");
    return -1;
  }
  copysize = next_ops->get_size (nxdata);
  if (copysize == -1)
    return -1;

  /* Run the tar command. */
  nbdkit_debug ("%s", cmd);
  fp = popen (cmd, "w");
  if (fp == NULL) {
    nbdkit_error ("tar: %m");
    return -1;
  }

  /* Now loop, writing data from the plugin (the tar file) until we
   * detect that tar has written something to the output file or we
   * run out of plugin.  We're making the assumption that the plugin
   * is not going to be sparse, which is probably true of most tar
   * files.
   */
  for (i = 0; i < copysize; i += bufsize) {
    int err, r;
    const int64_t count = MIN (bufsize, copysize-i);
    int64_t j;
    struct stat statbuf;

    r = next_ops->pread (nxdata, buf, count, i, 0, &err);
    if (r == -1) {
      errno = err;
      nbdkit_error ("pread: %m");
      pclose (fp);
      return -1;
    }
    for (j = 0; j < count;) {
      size_t written = fwrite (&buf[j], 1, count-j, fp);
      if (written == 0) {
        nbdkit_error ("tar: error writing to subprocess");
        pclose (fp);
        return -1;
      }
      j += written;
    }

    /* Did we get something in the output file yet? */
    if (stat (output, &statbuf) == 0 && statbuf.st_size > 0)
      break;
  }
  pclose (fp);

  /* Open the tar output and try to parse it. */
  fp = fopen (output, "r");
  if (fp == NULL) {
    nbdkit_error ("%s: %m", output);
    return -1;
  }
  scanned_ok = fscanf (fp, "block %" SCNu64 ": %*s %*s %" SCNu64,
                       &tar_offset, &tar_size) == 2;
  fclose (fp);
  unlink (output);

  if (!scanned_ok) {
    nbdkit_error ("tar subcommand failed, "
                  "check that the file really exists in the tarball");
    return -1;
  }

  /* Adjust the offset: Add 1 for the tar header, then multiply by the
   * block size.
   */
  tar_offset = (tar_offset+1) * 512;

  nbdkit_debug ("tar: %s found at offset %" PRIu64 ", size %" PRIu64,
                entry, tar_offset, tar_size);

  /* Check it looks sensible.  XXX We ought to check it doesn't exceed
   * the size of the tar file.
   */
  if (tar_offset >= INT64_MAX || tar_size >= INT64_MAX) {
    nbdkit_error ("internal error: calculated offset and size are wrong");
    return -1;
  }

  initialized = true;

  return 0;
}

static int
tar_prepare (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle, int readonly)
{
  struct handle *h = handle;
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);

  if (!initialized) {
    if (calculate_offset_of_entry (next_ops, nxdata) == -1)
      return -1;
  }

  assert (initialized);
  assert (tar_offset > 0);
  h->offset = tar_offset;
  h->size = tar_size;
  return 0;
}

/* Get the file size. */
static int64_t
tar_get_size (struct nbdkit_next_ops *next_ops, void *nxdata,
              void *handle)
{
  struct handle *h = handle;
  int64_t size;

  /* We must call underlying get_size even though we don't use the
   * result, because it caches the plugin size in server/backend.c.
   */
  size = next_ops->get_size (nxdata);
  if (size == -1)
    return -1;

  return h->size;
}

/* Read data from the file. */
static int
tar_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
           void *handle, void *buf, uint32_t count, uint64_t offs,
           uint32_t flags, int *err)
{
  struct handle *h = handle;
  return next_ops->pread (nxdata, buf, count, offs + h->offset, flags, err);
}

/* Write data to the file. */
static int
tar_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle, const void *buf, uint32_t count, uint64_t offs,
            uint32_t flags, int *err)
{
  struct handle *h = handle;
  return next_ops->pwrite (nxdata, buf, count, offs + h->offset, flags, err);
}

/* Trim data. */
static int
tar_trim (struct nbdkit_next_ops *next_ops, void *nxdata,
          void *handle, uint32_t count, uint64_t offs, uint32_t flags,
          int *err)
{
  struct handle *h = handle;
  return next_ops->trim (nxdata, count, offs + h->offset, flags, err);
}

/* Zero data. */
static int
tar_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
          void *handle, uint32_t count, uint64_t offs, uint32_t flags,
          int *err)
{
  struct handle *h = handle;
  return next_ops->zero (nxdata, count, offs + h->offset, flags, err);
}

/* Extents. */
static int
tar_extents (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle, uint32_t count, uint64_t offs, uint32_t flags,
             struct nbdkit_extents *extents, int *err)
{
  struct handle *h = handle;
  size_t i;
  CLEANUP_EXTENTS_FREE struct nbdkit_extents *extents2 = NULL;
  struct nbdkit_extent e;

  extents2 = nbdkit_extents_new (offs + h->offset, h->offset + h->size);
  if (extents2 == NULL) {
    *err = errno;
    return -1;
  }
  if (next_ops->extents (nxdata, count, offs + h->offset,
                         flags, extents2, err) == -1)
    return -1;

  for (i = 0; i < nbdkit_extents_count (extents2); ++i) {
    e = nbdkit_get_extent (extents2, i);
    e.offset -= h->offset;
    if (nbdkit_add_extent (extents, e.offset, e.length, e.type) == -1) {
      *err = errno;
      return -1;
    }
  }
  return 0;
}

/* Cache data. */
static int
tar_cache (struct nbdkit_next_ops *next_ops, void *nxdata,
           void *handle, uint32_t count, uint64_t offs, uint32_t flags,
           int *err)
{
  struct handle *h = handle;
  return next_ops->cache (nxdata, count, offs + h->offset, flags, err);
}

static struct nbdkit_filter filter = {
  .name              = "tar",
  .longname          = "nbdkit tar filter",
  .config            = tar_config,
  .config_complete   = tar_config_complete,
  .config_help       = tar_config_help,
  .thread_model      = tar_thread_model,
  .open              = tar_open,
  .close             = tar_close,
  .prepare           = tar_prepare,
  .get_size          = tar_get_size,
  .pread             = tar_pread,
  .pwrite            = tar_pwrite,
  .trim              = tar_trim,
  .zero              = tar_zero,
  .extents           = tar_extents,
  .cache             = tar_cache,
};

NBDKIT_REGISTER_FILTER(filter)
