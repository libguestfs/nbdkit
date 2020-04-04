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
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "utils.h"

static const char *tmpdir = "/var/tmp";
static int64_t size = -1;
static const char *label = NULL;
static const char *type = "ext4";

/* This comes from default-command.c which is generated from
 * default-command.sh.in.
 */
extern const char *command;

static void
tmpdisk_load (void)
{
  const char *s;

  s = getenv ("TMPDIR");
  if (s)
    tmpdir = s;
}

static int
tmpdisk_config (const char *key, const char *value)
{
  if (strcmp (key, "command") == 0) {
    command = value;
  }
  else if (strcmp (key, "label") == 0) {
    if (strcmp (value, "") == 0)
      label = NULL;
    else
      label = value;
  }
  else if (strcmp (key, "size") == 0) {
    size = nbdkit_parse_size (value);
    if (size == -1)
      return -1;
  }
  else if (strcmp (key, "type") == 0) {
    type = value;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int
tmpdisk_config_complete (void)
{
  if (size == -1) {
    nbdkit_error ("size parameter is required");
    return -1;
  }

  return 0;
}

#define tmpdisk_config_help \
  "size=<SIZE>      (required) Virtual filesystem size.\n" \
  "label=<LABEL>               The filesystem label.\n" \
  "type=ext4|...               The filesystem type.\n" \
  "command=<COMMAND>           Alternate command instead of mkfs."

struct handle {
  int fd;
  bool can_punch_hole;
};

/* Multi-conn is absolutely unsafe!  In this callback it is simply
 * returning the default value (no multi-conn), that's to make it
 * clear for future authors.
 */
static int
tmpdisk_can_multi_conn (void *handle)
{
  return 0;
}

static int
tmpdisk_can_trim (void *handle)
{
#ifdef FALLOC_FL_PUNCH_HOLE
  return 1;
#else
  return 0;
#endif
}

/* Pretend we have native FUA support, but actually because all disks
 * are temporary we will deliberately ignore flush/FUA operations.
 */
static int
tmpdisk_can_fua (void *handle)
{
  return NBDKIT_FUA_NATIVE;
}

static int64_t
tmpdisk_get_size (void *handle)
{
  return size;
}

/* This creates and runs the full "mkfs" (or whatever) command. */
static int
run_command (const char *disk)
{
  FILE *fp;
  CLEANUP_FREE char *cmd = NULL;
  size_t len = 0;
  int r;

  fp = open_memstream (&cmd, &len);
  if (fp == NULL) {
    nbdkit_error ("open_memstream: %m");
    return -1;
  }

  /* Avoid stdin/stdout leaking (because of nbdkit -s). */
  fprintf (fp, "exec </dev/null >/dev/null\n");

  /* Set the shell variables. */
  fprintf (fp, "disk=");
  shell_quote (disk, fp);
  putc ('\n', fp);
  if (label) {
    fprintf (fp, "label=");
    shell_quote (label, fp);
    putc ('\n', fp);
  }
  fprintf (fp, "size=%" PRIi64 "\n", size);
  fprintf (fp, "type=");
  shell_quote (type, fp);
  putc ('\n', fp);

  putc ('\n', fp);
  fprintf (fp, "%s", command);

  if (fclose (fp) == EOF) {
    nbdkit_error ("memstream failed");
    return -1;
  }

  r = system (cmd);
  if (r == -1) {
    nbdkit_error ("failed to execute command: %m");
    return -1;
  }
  if (WIFEXITED (r) && WEXITSTATUS (r) != 0) {
    nbdkit_error ("command exited with code %d", WEXITSTATUS (r));
    return -1;
  }
  else if (WIFSIGNALED (r)) {
    nbdkit_error ("command killed by signal %d", WTERMSIG (r));
    return -1;
  }
  else if (WIFSTOPPED (r)) {
    nbdkit_error ("command stopped by signal %d", WSTOPSIG (r));
    return -1;
  }

  return 0;
}

static void *
tmpdisk_open (int readonly)
{
  struct handle *h;
  CLEANUP_FREE char *disk = NULL;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    goto error;
  }
  h->fd = -1;
  h->can_punch_hole = true;

  /* Create the new disk image for this connection. */
  if (asprintf (&disk, "%s/tmpdiskXXXXXX", tmpdir) == -1) {
    nbdkit_error ("asprintf: %m");
    goto error;
  }

#ifdef HAVE_MKOSTEMP
  h->fd = mkostemp (disk, O_CLOEXEC);
#else
  /* Racy, fix your libc. */
  h->fd = mkstemp (disk);
  if (h->fd >= 0) {
    h->fd = set_cloexec (h->fd);
    if (h->fd == -1) {
      int e = errno;
      unlink (disk);
      errno = e;
    }
  }
#endif
  if (h->fd == -1) {
    nbdkit_error ("mkstemp: %m");
    goto error;
  }

  /* Truncate the disk to a sparse file of the right size. */
  if (ftruncate (h->fd, size) == -1) {
    nbdkit_error ("ftruncate: %s: %m", disk);
    goto error;
  }

  /* Now run the mkfs command. */
  if (run_command (disk) == -1)
    goto error;

  /* We don't need the disk to appear in the filesystem since we hold
   * a file descriptor and access it through that, so unlink the disk.
   * This also ensures it is always cleaned up.
   */
  unlink (disk);

  /* Return the handle. */
  return h;

 error:
  if (h) {
    if (h->fd >= 0) {
      close (h->fd);
      unlink (disk);
    }
    free (h);
  }
  return NULL;
}

static void
tmpdisk_close (void *handle)
{
  struct handle *h = handle;

  close (h->fd);
  free (h);
}

/* Read data from the file. */
static int
tmpdisk_pread (void *handle, void *buf,
               uint32_t count, uint64_t offset,
               uint32_t flags)
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
tmpdisk_pwrite (void *handle, const void *buf,
                uint32_t count, uint64_t offset,
                uint32_t flags)
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

  /* Deliberately ignore FUA if present in flags. */

  return 0;
}

/* This plugin deliberately provides a null flush operation, because
 * all of the disks created are temporary.
 */
static int
tmpdisk_flush (void *handle, uint32_t flags)
{
  return 0;
}

#if defined (FALLOC_FL_PUNCH_HOLE)
static int
do_fallocate (int fd, int mode, off_t offset, off_t len)
{
  int r = fallocate (fd, mode, offset, len);
  if (r == -1 && errno == ENODEV) {
    /* kernel 3.10 fails with ENODEV for block device. Kernel >= 4.9 fails
     * with EOPNOTSUPP in this case. Normalize errno to simplify callers.
     */
    errno = EOPNOTSUPP;
  }
  return r;
}

static bool
is_enotsup (int err)
{
  return err == ENOTSUP || err == EOPNOTSUPP;
}
#endif

/* Punch a hole in the file. */
static int
tmpdisk_trim (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
#ifdef FALLOC_FL_PUNCH_HOLE
  struct handle *h = handle;
  int r;

  if (h->can_punch_hole) {
    r = do_fallocate (h->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                      offset, count);
    if (r == -1) {
      /* Trim is advisory; we don't care if it fails for anything other
       * than EIO or EPERM.
       */
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

  /* Deliberately ignore FUA if present in flags. */

  return 0;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

static struct nbdkit_plugin plugin = {
  .name              = "tmpdisk",
  .version           = PACKAGE_VERSION,

  .load              = tmpdisk_load,
  .config            = tmpdisk_config,
  .config_complete   = tmpdisk_config_complete,
  .config_help       = tmpdisk_config_help,
  .magic_config_key  = "size",

  .can_multi_conn    = tmpdisk_can_multi_conn,
  .can_trim          = tmpdisk_can_trim,
  .can_fua           = tmpdisk_can_fua,
  .get_size          = tmpdisk_get_size,

  .open              = tmpdisk_open,
  .close             = tmpdisk_close,
  .pread             = tmpdisk_pread,
  .pwrite            = tmpdisk_pwrite,
  .flush             = tmpdisk_flush,
  .trim              = tmpdisk_trim,

  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
