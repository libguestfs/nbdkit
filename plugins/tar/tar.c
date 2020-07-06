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
#include <fcntl.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "utils.h"

static char *tarfile;           /* The tar file (tar= parameter). */
static const char *file;        /* File within tar (file=). */
static uint64_t offset, size;   /* Offset and size within tarball. */

static void
tar_unload (void)
{
  free (tarfile);
}

static int
tar_config (const char *key, const char *value)
{
  if (strcmp (key, "tar") == 0) {
    if (tarfile) {
      nbdkit_error ("only one tar parameter can be given");
      return -1;
    }
    tarfile = nbdkit_realpath (value);
    if (tarfile == NULL)
      return -1;
  }
  else if (strcmp (key, "file") == 0) {
    if (file) {
      nbdkit_error ("only one file parameter can be given");
      return -1;
    }
    file = value;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int
tar_config_complete (void)
{
  if (tarfile == NULL || file == NULL) {
    nbdkit_error ("you must supply the tar=<TARFILE> and file=<FILENAME> "
                  "parameters");
    return -1;
  }

  return 0;
}

#define tar_config_help \
  "[tar=]<TARBALL>     (required) The name of the tar file.\n" \
  "file=<FILENAME>     (required) The path inside the tar file to serve."

static int
tar_get_ready (void)
{
  FILE *fp;
  CLEANUP_FREE char *cmd = NULL;
  size_t len = 0;
  bool scanned_ok;
  char s[256];

  /* Construct the tar command to examine the tar file. */
  fp = open_memstream (&cmd, &len);
  if (fp == NULL) {
    nbdkit_error ("open_memstream: %m");
    return -1;
  }
  fprintf (fp, "LANG=C tar --no-auto-compress -tRvf ");
  shell_quote (tarfile, fp);
  fputc (' ', fp);
  shell_quote (file, fp);
  if (fclose (fp) == EOF) {
    nbdkit_error ("memstream failed: %m");
    return -1;
  }

  /* Run the command and read the first line of the output. */
  nbdkit_debug ("%s", cmd);
  fp = popen (cmd, "r");
  if (fp == NULL) {
    nbdkit_error ("tar: %m");
    return -1;
  }
  scanned_ok = fscanf (fp, "block %" SCNu64 ": %*s %*s %" SCNu64,
                       &offset, &size) == 2;
  /* We have to now read and discard the rest of the output until EOF. */
  while (fread (s, sizeof s, 1, fp) > 0)
    ;
  if (pclose (fp) != 0) {
    nbdkit_error ("tar subcommand failed, "
                  "check that the file really exists in the tarball");
    return -1;
  }

  if (!scanned_ok) {
    nbdkit_error ("unexpected output from the tar subcommand");
    return -1;
  }

  /* Adjust the offset: Add 1 for the tar header, then multiply by the
   * block size.
   */
  offset = (offset+1) * 512;

  nbdkit_debug ("tar: offset %" PRIu64 ", size %" PRIu64, offset, size);

  /* Check it looks sensible.  XXX We ought to check it doesn't exceed
   * the size of the tar file.
   */
  if (offset >= INT64_MAX || size >= INT64_MAX) {
    nbdkit_error ("internal error: calculated offset and size are wrong");
    return -1;
  }

  return 0;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

struct handle {
  int fd;
};

static void *
tar_open (int readonly)
{
  struct handle *h;

  assert (offset > 0);     /* Cannot be zero because of tar header. */

  h = calloc (1, sizeof *h);
  if (h == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }
  h->fd = open (tarfile, (readonly ? O_RDONLY : O_RDWR) | O_CLOEXEC);
  if (h->fd == -1) {
    nbdkit_error ("%s: %m", tarfile);
    free (h);
    return NULL;
  }

  return h;
}

static void
tar_close (void *handle)
{
  struct handle *h = handle;

  close (h->fd);
}

/* Get the file size. */
static int64_t
tar_get_size (void *handle)
{
  return size;
}

/* Serves the same data over multiple connections. */
static int
tar_can_multi_conn (void *handle)
{
  return 1;
}

static int
tar_can_cache (void *handle)
{
  /* Let nbdkit call pread to populate the file system cache. */
  return NBDKIT_CACHE_EMULATE;
}

/* Read data from the file. */
static int
tar_pread (void *handle, void *buf, uint32_t count, uint64_t offs)
{
  struct handle *h = handle;

  offs += offset;

  while (count > 0) {
    ssize_t r = pread (h->fd, buf, count, offs);
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
    offs += r;
  }

  return 0;
}

/* Write data to the file. */
static int
tar_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offs)
{
  struct handle *h = handle;

  offs += offset;

  while (count > 0) {
    ssize_t r = pwrite (h->fd, buf, count, offs);
    if (r == -1) {
      nbdkit_error ("pwrite: %m");
      return -1;
    }
    buf += r;
    count -= r;
    offs += r;
  }

  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "tar",
  .longname          = "nbdkit tar plugin",
  .version           = PACKAGE_VERSION,
  .unload            = tar_unload,
  .config            = tar_config,
  .config_complete   = tar_config_complete,
  .config_help       = tar_config_help,
  .magic_config_key  = "tar",
  .get_ready         = tar_get_ready,
  .open              = tar_open,
  .close             = tar_close,
  .get_size          = tar_get_size,
  .can_multi_conn    = tar_can_multi_conn,
  .can_cache         = tar_can_cache,
  .pread             = tar_pread,
  .pwrite            = tar_pwrite,
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
