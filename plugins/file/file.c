/* nbdkit
 * Copyright (C) 2013-2018 Red Hat Inc.
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
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>

#if defined(__linux__) && !defined(FALLOC_FL_PUNCH_HOLE)
#include <linux/falloc.h>   /* For FALLOC_FL_*, glibc < 2.18 */
#endif

#if defined(__linux__)
#include <linux/fs.h>       /* For BLKZEROOUT */
#endif

#include <nbdkit-plugin.h>

#include "isaligned.h"

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#ifndef HAVE_FDATASYNC
#define fdatasync fsync
#endif

static char *filename = NULL;

int file_debug_zero;            /* to enable: -D file.zero=1 */

static void
file_unload (void)
{
  free (filename);
}

/* Called for each key=value passed on the command line.  This plugin
 * only accepts file=<filename>, which is required.
 */
static int
file_config (const char *key, const char *value)
{
  if (strcmp (key, "file") == 0) {
    /* See FILENAMES AND PATHS in nbdkit-plugin(3). */
    free (filename);
    filename = nbdkit_realpath (value);
    if (!filename)
      return -1;
  }
  else if (strcmp (key, "rdelay") == 0 ||
           strcmp (key, "wdelay") == 0) {
    nbdkit_error ("add --filter=delay on the command line");
    return -1;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

/* Check the user did pass a file=<FILENAME> parameter. */
static int
file_config_complete (void)
{
  if (filename == NULL) {
    nbdkit_error ("you must supply the file=<FILENAME> parameter "
                  "after the plugin name on the command line");
    return -1;
  }

  return 0;
}

#define file_config_help \
  "file=<FILENAME>     (required) The filename to serve." \

/* Print some extra information about how the plugin was compiled. */
static void
file_dump_plugin (void)
{
#ifdef BLKSSZGET
  printf ("file_blksszget=yes\n");
#endif
#ifdef BLKZEROOUT
  printf ("file_blkzeroout=yes\n");
#endif
#ifdef FALLOC_FL_PUNCH_HOLE
  printf ("file_falloc_fl_punch_hole=yes\n");
#endif
#ifdef FALLOC_FL_ZERO_RANGE
  printf ("file_falloc_fl_zero_range=yes\n");
#endif
}

/* The per-connection handle. */
struct handle {
  int fd;
  bool is_block_device;
  int sector_size;
  bool can_punch_hole;
  bool can_zero_range;
  bool can_fallocate;
  bool can_zeroout;
};

/* Create the per-connection handle. */
static void *
file_open (int readonly)
{
  struct handle *h;
  struct stat statbuf;
  int flags;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  flags = O_CLOEXEC|O_NOCTTY;
  if (readonly)
    flags |= O_RDONLY;
  else
    flags |= O_RDWR;

  h->fd = open (filename, flags);
  if (h->fd == -1) {
    nbdkit_error ("open: %s: %m", filename);
    free (h);
    return NULL;
  }

  if (fstat (h->fd, &statbuf) == -1) {
    nbdkit_error ("fstat: %s: %m", filename);
    free (h);
    return NULL;
  }

  h->is_block_device = S_ISBLK(statbuf.st_mode);
  h->sector_size = 4096;  /* Start with safe guess */

#ifdef BLKSSZGET
  if (h->is_block_device) {
    if (ioctl (h->fd, BLKSSZGET, &h->sector_size))
      nbdkit_debug ("cannot get sector size: %s: %m", filename);
  }
#endif

#ifdef FALLOC_FL_PUNCH_HOLE
  h->can_punch_hole = true;
#else
  h->can_punch_hole = false;
#endif

#ifdef FALLOC_FL_ZERO_RANGE
  h->can_zero_range = true;
#else
  h->can_zero_range = false;
#endif

  h->can_fallocate = true;
  h->can_zeroout = h->is_block_device;

  return h;
}

/* Free up the per-connection handle. */
static void
file_close (void *handle)
{
  struct handle *h = handle;

  close (h->fd);
  free (h);
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Get the file size. */
static int64_t
file_get_size (void *handle)
{
  struct handle *h = handle;

  if (h->is_block_device) {
    /* Block device, so st_size will not be the true size. */
    off_t size;

    size = lseek (h->fd, 0, SEEK_END);
    if (size == -1) {
      nbdkit_error ("lseek (to find device size): %m");
      return -1;
    }

    return size;
  } else {
    /* Regular file. */
    struct stat statbuf;

    if (fstat (h->fd, &statbuf) == -1) {
      nbdkit_error ("fstat: %m");
      return -1;
    }

    return statbuf.st_size;
  }
}

static int
file_can_trim (void *handle)
{
  /* Trim is advisory, but we prefer to advertise it only when we can
   * actually (attempt to) punch holes.  Since not all filesystems
   * support all fallocate modes, it would be nice if we had a way
   * from fpathconf() to definitively learn what will work on a given
   * fd for a more precise answer; oh well.  */
#ifdef FALLOC_FL_PUNCH_HOLE
  return 1;
#else
  return 0;
#endif
}

/* Read data from the file. */
static int
file_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  struct handle *h = handle;

  while (count > 0) {
    ssize_t r = pread (h->fd, buf, count, offset);
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
file_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset)
{
  struct handle *h = handle;

  while (count > 0) {
    ssize_t r = pwrite (h->fd, buf, count, offset);
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

#if defined(FALLOC_FL_PUNCH_HOLE) || defined(FALLOC_FL_ZERO_RANGE)
static int
do_fallocate(int fd, int mode, off_t offset, off_t len)
{
  int r = fallocate (fd, mode, offset, len);
  if (r == -1 && errno == ENODEV) {
    /* kernel 3.10 fails with ENODEV for block device. Kernel >= 4.9 fails
       with EOPNOTSUPP in this case. Normalize errno to simplify callers. */
    errno = EOPNOTSUPP;
  }
  return r;
}
#endif

/* Write zeroes to the file. */
static int
file_zero (void *handle, uint32_t count, uint64_t offset, int may_trim)
{
  struct handle *h = handle;
  int r;

#ifdef FALLOC_FL_PUNCH_HOLE
  if (h->can_punch_hole && may_trim) {
    r = do_fallocate (h->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                      offset, count);
    if (r == 0) {
      if (file_debug_zero)
        nbdkit_debug ("h->can_punch_hole && may_trim: "
                      "zero succeeded using fallocate");
      return 0;
    }

    if (errno != EOPNOTSUPP) {
      nbdkit_error ("zero: %m");
      return -1;
    }

    h->can_punch_hole = false;
  }
#endif

#ifdef FALLOC_FL_ZERO_RANGE
  if (h->can_zero_range) {
    r = do_fallocate (h->fd, FALLOC_FL_ZERO_RANGE, offset, count);
    if (r == 0) {
      if (file_debug_zero)
        nbdkit_debug ("h->can_zero-range: "
                      "zero succeeded using fallocate");
      return 0;
    }

    if (errno != EOPNOTSUPP) {
      nbdkit_error ("zero: %m");
      return -1;
    }

    h->can_zero_range = false;
  }
#endif

#ifdef FALLOC_FL_PUNCH_HOLE
  /* If we can punch hole but may not trim, we can combine punching hole and
   * fallocate to zero a range. This is expected to be more efficient than
   * writing zeros manually. */
  if (h->can_punch_hole && h->can_fallocate) {
    r = do_fallocate (h->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                      offset, count);
    if (r == 0) {
      r = do_fallocate (h->fd, 0, offset, count);
      if (r == 0) {
        if (file_debug_zero)
          nbdkit_debug ("h->can_punch_hole && h->can_fallocate: "
                        "zero succeeded using fallocate");
        return 0;
      }

      if (errno != EOPNOTSUPP) {
        nbdkit_error ("zero: %m");
        return -1;
      }

      h->can_fallocate = false;
    } else {
      if (errno != EOPNOTSUPP) {
        nbdkit_error ("zero: %m");
        return -1;
      }

      h->can_punch_hole = false;
    }
  }
#endif

#ifdef BLKZEROOUT
  /* For aligned range and block device, we can use BLKZEROOUT. */
  if (h->can_zeroout && IS_ALIGNED (offset | count, h->sector_size)) {
    uint64_t range[2] = {offset, count};

    r = ioctl (h->fd, BLKZEROOUT, &range);
    if (r == 0) {
      if (file_debug_zero)
        nbdkit_debug ("h->can_zeroout && IS_ALIGNED: "
                      "zero succeeded using BLKZEROOUT");
      return 0;
    }

    if (errno != ENOTTY) {
      nbdkit_error ("zero: %m");
      return -1;
    }

    h->can_zeroout = false;
  }
#endif

  /* Trigger a fall back to writing */
  if (file_debug_zero)
    nbdkit_debug ("zero falling back to writing");
  errno = EOPNOTSUPP;
  return -1;
}

/* Flush the file to disk. */
static int
file_flush (void *handle)
{
  struct handle *h = handle;

  if (fdatasync (h->fd) == -1) {
    nbdkit_error ("fdatasync: %m");
    return -1;
  }

  return 0;
}

/* Punch a hole in the file. */
static int
file_trim (void *handle, uint32_t count, uint64_t offset)
{
#ifdef FALLOC_FL_PUNCH_HOLE
  struct handle *h = handle;
  int r;

  if (h->can_punch_hole) {
    r = do_fallocate (h->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                      offset, count);
    if (r == -1) {
      /* Trim is advisory; we don't care if it fails for anything other
       * than EIO or EPERM. */
      if (errno == EPERM || errno == EIO) {
        nbdkit_error ("fallocate: %m");
        return -1;
      }

      if (errno == EOPNOTSUPP)
        h->can_punch_hole = false;

      nbdkit_debug ("ignoring failed fallocate during trim: %m");
    }
  }
#endif

  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "file",
  .longname          = "nbdkit file plugin",
  .version           = PACKAGE_VERSION,
  .unload            = file_unload,
  .config            = file_config,
  .config_complete   = file_config_complete,
  .config_help       = file_config_help,
  .magic_config_key  = "file",
  .dump_plugin       = file_dump_plugin,
  .open              = file_open,
  .close             = file_close,
  .get_size          = file_get_size,
  .can_trim          = file_can_trim,
  .pread             = file_pread,
  .pwrite            = file_pwrite,
  .flush             = file_flush,
  .trim              = file_trim,
  .zero              = file_zero,
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
