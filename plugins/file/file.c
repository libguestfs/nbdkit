/* nbdkit
 * Copyright (C) 2013-2020 Red Hat Inc.
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
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#include <pthread.h>

#if defined (__linux__) && !defined (FALLOC_FL_PUNCH_HOLE)
#include <linux/falloc.h>   /* For FALLOC_FL_*, glibc < 2.18 */
#endif

#if defined (__linux__)
#include <linux/fs.h>       /* For BLKZEROOUT */
#endif

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "isaligned.h"
#include "fdatasync.h"
#include "pread.h"
#include "pwrite.h"
#include "windows-compat.h"

static char *filename = NULL;
static char *directory = NULL;

/* posix_fadvise mode: -1 = don't set it, or POSIX_FADV_*. */
static int fadvise_mode =
#if defined (HAVE_POSIX_FADVISE) && defined (POSIX_FADV_NORMAL)
  POSIX_FADV_NORMAL
#else
  -1
#endif
  ;

/* cache mode */
static enum { cache_default, cache_none } cache_mode = cache_default;

/* Any callbacks using lseek must be protected by this lock. */
static pthread_mutex_t lseek_lock = PTHREAD_MUTEX_INITIALIZER;

/* to enable: -D file.zero=1 */
int file_debug_zero;

static bool
is_enotsup (int err)
{
  return err == ENOTSUP || err == EOPNOTSUPP;
}

static void
file_unload (void)
{
  free (filename);
  free (directory);
}

/* Called for each key=value passed on the command line.  This plugin
 * only accepts file=<filename> and dir=<dirname>, where exactly
 * one is required.
 */
static int
file_config (const char *key, const char *value)
{
  /* See FILENAMES AND PATHS in nbdkit-plugin(3).
   * Our use of nbdkit_realpath requires the destination to exist at
   * startup; use nbdkit_absolute_path instead if we wanted to defer
   * existence checks to the last possible moment.
   */
  if (strcmp (key, "file") == 0) {
    free (filename);
    filename = nbdkit_realpath (value);
    if (!filename)
      return -1;
  }
#ifndef WIN32
  else if (strcmp (key, "directory") == 0 ||
           strcmp (key, "dir") == 0) {
    free (directory);
    directory = nbdkit_realpath (value);
    if (!directory)
      return -1;
  }
#endif
  else if (strcmp (key, "fadvise") == 0) {
    /* As this is a hint, if the kernel doesn't support the feature
     * ignore the parameter.
     */
    if (strcmp (value, "normal") == 0) {
#if defined (HAVE_POSIX_FADVISE) && defined (POSIX_FADV_NORMAL)
      fadvise_mode = POSIX_FADV_NORMAL;
#else
      fadvise_mode = -1;
#endif
    }
    else if (strcmp (value, "random") == 0) {
#if defined (HAVE_POSIX_FADVISE) && defined (POSIX_FADV_RANDOM)
      fadvise_mode = POSIX_FADV_RANDOM;
#else
      fadvise_mode = -1;
#endif
    }
    else if (strcmp (value, "sequential") == 0) {
#if defined (HAVE_POSIX_FADVISE) && defined (POSIX_FADV_SEQUENTIAL)
      fadvise_mode = POSIX_FADV_SEQUENTIAL;
#else
      fadvise_mode = -1;
#endif
    }
    else {
      nbdkit_error ("unknown fadvise mode: %s", value);
      return -1;
    }
  }
  else if (strcmp (key, "cache") == 0) {
    if (strcmp (value, "default") == 0)
      cache_mode = cache_default;
    else if (strcmp (value, "none") == 0)
      cache_mode = cache_none;
    else {
      nbdkit_error ("unknown cache mode: %s", value);
      return -1;
    }
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

/* Check the user passed exactly one parameter. */
static int
file_config_complete (void)
{
  int r;
  struct stat sb;

  if (!filename && !directory) {
    nbdkit_error ("you must supply either [file=]<FILENAME> or "
                  "dir=<DIRNAME> parameter after the plugin name "
                  "on the command line");
    return -1;
  }
  if (filename && directory) {
    nbdkit_error ("file= and dir= cannot be used at the same time");
    return -1;
  }

  /* Sanity check now, rather than waiting for first client open.
   * See also comment in .config about use of nbdkit_realpath.
   * Yes, this is a harmless TOCTTOU race.
   */
  if (filename) {
    r = stat (filename, &sb);
    if (r == 0 && S_ISDIR (sb.st_mode)) {
      nbdkit_error ("use dir= to serve files within %s", filename);
      return -1;
    }
    if (r == -1 || !(S_ISBLK (sb.st_mode) || S_ISREG (sb.st_mode))) {
      nbdkit_error ("file is not regular or block device: %s", filename);
      return -1;
    }
  }
  else if (stat (directory, &sb) == -1 || !S_ISDIR (sb.st_mode)) {
    nbdkit_error ("expecting a directory: %s", directory);
    return -1;
  }

  return 0;
}

#define file_config_help \
  "[file=]<FILENAME>     The filename to serve.\n" \
  "dir=<DIRNAME>         A directory containing files to serve.\n" \
  "cache=<MODE>          Set use of caching (default, none).\n" \
  "fadise=<LEVEL>        Set fadvise hint (normal, random, sequential).\n" \

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
  const char *file;
  int dfd = -1;

  if (directory) {
#ifndef WIN32
    file = nbdkit_export_name ();
    if (strchr (file, '/')) {
      nbdkit_error ("exportname cannot contain /");
      errno = EINVAL;
      return NULL;
    }
    dfd = open (directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd == -1) {
      nbdkit_error ("open %s: %m", directory);
      return NULL;
    }
#else
    abort ();
#endif
  }
  else
    file = filename;

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

#ifndef WIN32
  h->fd = openat (dfd, file, flags);
#else
  h->fd = open (file, flags);
#endif
  if (h->fd == -1) {
    nbdkit_error ("openat: %s: %m", file);
    if (dfd != -1)
      close (dfd);
    free (h);
    return NULL;
  }
  if (dfd != -1)
    close (dfd);

  if (fstat (h->fd, &statbuf) == -1) {
    nbdkit_error ("fstat: %s: %m", file);
    free (h);
    return NULL;
  }

  if (fadvise_mode != -1) {
    /* This is a hint so we ignore failures. */
#ifdef HAVE_POSIX_FADVISE
    int r = posix_fadvise (h->fd, 0, 0, fadvise_mode);
    if (r == -1)
      nbdkit_debug ("posix_fadvise: %s: %m (ignored)", filename);
#else
    nbdkit_debug ("fadvise is not supported");
#endif
  }

  if (S_ISBLK (statbuf.st_mode))
    h->is_block_device = true;
  else if (S_ISREG (statbuf.st_mode))
    h->is_block_device = false;
  else {
    nbdkit_error ("file is not regular or block device: %s", file);
    close (h->fd);
    free (h);
    return NULL;
  }
  h->sector_size = 4096;  /* Start with safe guess */

#ifdef BLKSSZGET
  if (h->is_block_device) {
    if (ioctl (h->fd, BLKSSZGET, &h->sector_size))
      nbdkit_debug ("cannot get sector size: %s: %m", file);
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

/* For block devices, stat->st_size is not the true size.  The caller
 * grabs the lseek_lock.
 */
static int64_t
block_device_size (int fd)
{
  off_t size;

  size = lseek (fd, 0, SEEK_END);
  if (size == -1) {
    nbdkit_error ("lseek (to find device size): %m");
    return -1;
  }

  return size;
}

/* Get the file size. */
static int64_t
file_get_size (void *handle)
{
  struct handle *h = handle;

  if (h->is_block_device) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lseek_lock);
    return block_device_size (h->fd);
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

/* Allow multiple parallel connections from a single client. */
static int
file_can_multi_conn (void *handle)
{
  return 1;
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

static int
file_can_fua (void *handle)
{
  return NBDKIT_FUA_NATIVE;
}

static int
file_can_cache (void *handle)
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

/* Flush the file to disk. */
static int
file_flush (void *handle, uint32_t flags)
{
  struct handle *h = handle;

  if (fdatasync (h->fd) == -1) {
    nbdkit_error ("fdatasync: %m");
    return -1;
  }

  return 0;
}

/* Read data from the file. */
static int
file_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
            uint32_t flags)
{
  struct handle *h = handle;
#if defined (HAVE_POSIX_FADVISE) && defined (POSIX_FADV_DONTNEED)
  uint32_t orig_count = count;
  uint64_t orig_offset = offset;
#endif

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

#if defined (HAVE_POSIX_FADVISE) && defined (POSIX_FADV_DONTNEED)
  /* On Linux this will evict the pages we just read from the page cache. */
  if (cache_mode == cache_none)
    posix_fadvise (h->fd, orig_offset, orig_count, POSIX_FADV_DONTNEED);
#endif

  return 0;
}

/* Write data to the file. */
static int
file_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
             uint32_t flags)
{
  struct handle *h = handle;

#if defined (HAVE_POSIX_FADVISE) && defined (POSIX_FADV_DONTNEED)
  uint32_t orig_count = count;
  uint64_t orig_offset = offset;

  /* If cache=none we want to force pages we have just written to the
   * file to be flushed to disk so we can immediately evict them from
   * the page cache.
   */
  if (cache_mode == cache_none) flags |= NBDKIT_FLAG_FUA;
#endif

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

  if ((flags & NBDKIT_FLAG_FUA) && file_flush (handle, 0) == -1)
    return -1;

#if defined (HAVE_POSIX_FADVISE) && defined (POSIX_FADV_DONTNEED)
  /* On Linux this will evict the pages we just wrote from the page cache. */
  if (cache_mode == cache_none)
    posix_fadvise (h->fd, orig_offset, orig_count, POSIX_FADV_DONTNEED);
#endif

  return 0;
}

#if defined (FALLOC_FL_PUNCH_HOLE) || defined (FALLOC_FL_ZERO_RANGE)
static int
do_fallocate (int fd, int mode, off_t offset, off_t len)
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
file_zero (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  struct handle *h = handle;
  int r;

#ifdef FALLOC_FL_PUNCH_HOLE
  if (h->can_punch_hole && (flags & NBDKIT_FLAG_MAY_TRIM)) {
    r = do_fallocate (h->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                      offset, count);
    if (r == 0) {
      if (file_debug_zero)
        nbdkit_debug ("h->can_punch_hole && may_trim: "
                      "zero succeeded using fallocate");
      goto out;
    }

    if (!is_enotsup (errno)) {
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
      goto out;
    }

    if (!is_enotsup (errno)) {
      nbdkit_error ("zero: %m");
      return -1;
    }

    h->can_zero_range = false;
  }
#endif

#ifdef FALLOC_FL_PUNCH_HOLE
  /* If we can punch hole but may not trim, we can combine punching hole and
   * fallocate to zero a range. This is expected to be more efficient than
   * writing zeroes manually. */
  if (h->can_punch_hole && h->can_fallocate) {
    r = do_fallocate (h->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                      offset, count);
    if (r == 0) {
      r = do_fallocate (h->fd, 0, offset, count);
      if (r == 0) {
        if (file_debug_zero)
          nbdkit_debug ("h->can_punch_hole && h->can_fallocate: "
                        "zero succeeded using fallocate");
        goto out;
      }

      if (!is_enotsup (errno)) {
        nbdkit_error ("zero: %m");
        return -1;
      }

      h->can_fallocate = false;
    } else {
      if (!is_enotsup (errno)) {
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
      goto out;
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

 out:
  if ((flags & NBDKIT_FLAG_FUA) && file_flush (handle, 0) == -1)
    return -1;
  return 0;
}

/* Punch a hole in the file. */
static int
file_trim (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
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

      if (is_enotsup (EOPNOTSUPP))
        h->can_punch_hole = false;

      nbdkit_debug ("ignoring failed fallocate during trim: %m");
    }
  }
#endif

  if ((flags & NBDKIT_FLAG_FUA) && file_flush (handle, 0) == -1)
    return -1;

  return 0;
}

#ifdef SEEK_HOLE
/* Extents. */

static int
file_can_extents (void *handle)
{
  struct handle *h = handle;
  off_t r;

  /* A simple test to see whether SEEK_HOLE etc is likely to work on
   * the current filesystem.
   */
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lseek_lock);
  r = lseek (h->fd, 0, SEEK_HOLE);
  if (r == -1) {
    nbdkit_debug ("extents disabled: lseek: SEEK_HOLE: %m");
    return 0;
  }
  return 1;
}

static int
do_extents (void *handle, uint32_t count, uint64_t offset,
            uint32_t flags, struct nbdkit_extents *extents)
{
  struct handle *h = handle;
  const bool req_one = flags & NBDKIT_FLAG_REQ_ONE;
  uint64_t end = offset + count;

  do {
    off_t pos;

    pos = lseek (h->fd, offset, SEEK_DATA);
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
      if (nbdkit_add_extent (extents, offset, pos - offset,
                             NBDKIT_EXTENT_HOLE | NBDKIT_EXTENT_ZERO) == -1)
        return -1;
      if (req_one)
        break;
    }

    offset = pos;
    if (offset >= end)
      break;

    pos = lseek (h->fd, offset, SEEK_HOLE);
    if (pos == -1) {
      nbdkit_error ("lseek: SEEK_HOLE: %" PRIu64 ": %m", offset);
      return -1;
    }

    /* We know there is data from offset to pos-1. */
    if (pos > offset) {
      if (nbdkit_add_extent (extents, offset, pos - offset,
                             0 /* allocated data */) == -1)
        return -1;
      if (req_one)
        break;
    }

    offset = pos;
  } while (offset < end);

  return 0;
}

static int
file_extents (void *handle, uint32_t count, uint64_t offset,
              uint32_t flags, struct nbdkit_extents *extents)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lseek_lock);
  return do_extents (handle, count, offset, flags, extents);
}
#endif /* SEEK_HOLE */

#if HAVE_POSIX_FADVISE
/* Caching. */
static int
file_cache (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  struct handle *h = handle;
  int r;

  /* Cache is advisory, we don't care if this fails */
  r = posix_fadvise (h->fd, offset, count, POSIX_FADV_WILLNEED);
  if (r) {
    errno = r;
    nbdkit_error ("posix_fadvise: %m");
    return -1;
  }
  return 0;
}
#endif /* HAVE_POSIX_FADVISE */

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
  .can_multi_conn    = file_can_multi_conn,
  .can_trim          = file_can_trim,
  .can_fua           = file_can_fua,
  .can_cache         = file_can_cache,
  .pread             = file_pread,
  .pwrite            = file_pwrite,
  .flush             = file_flush,
  .trim              = file_trim,
  .zero              = file_zero,
#ifdef SEEK_HOLE
  .can_extents       = file_can_extents,
  .extents           = file_extents,
#endif
#if HAVE_POSIX_FADVISE
  .cache             = file_cache,
#endif
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
