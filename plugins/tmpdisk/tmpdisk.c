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
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "utils.h"

static const char *tmpdir = "/var/tmp";
static int64_t requested_size = -1; /* size parameter on the command line */

/* Shell variables. */
static struct var {
  struct var *next;
  const char *key, *value;
} *vars, *last_var;

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

static void
tmpdisk_unload (void)
{
  struct var *v, *v_next;

  for (v = vars; v != NULL; v = v_next) {
    v_next = v->next;
    free (v);
  }
}

static int
tmpdisk_config (const char *key, const char *value)
{
  if (strcmp (key, "command") == 0) {
    command = value;
  }
  else if (strcmp (key, "size") == 0) {
    requested_size = nbdkit_parse_size (value);
    if (requested_size == -1)
      return -1;
  }

  /* This parameter cannot be set on the command line since it is used
   * to pass the disk name to the command.
   */
  else if (strcmp (key, "disk") == 0) {
    nbdkit_error ("'disk' parameter cannot be set on the command line");
    return -1;
  }

  /* Any other parameter will be forwarded to a shell variable. */
  else {
    struct var *new_var;

    new_var = malloc (sizeof *new_var);
    if (new_var == NULL) {
      perror ("malloc");
      exit (EXIT_FAILURE);
    }

    new_var->next = NULL;
    new_var->key = key;
    new_var->value = value;

    /* Append it to the linked list. */
    if (vars == NULL) {
      assert (last_var == NULL);
      vars = last_var = new_var;
    }
    else {
      assert (last_var != NULL);
      last_var->next = new_var;
      last_var = new_var;
    }
  }

  return 0;
}

static int
tmpdisk_config_complete (void)
{
  if (requested_size == -1) {
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
  int64_t size;
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
  struct handle *h = handle;

  return h->size;
}

/* This creates and runs the full "mkfs" (or whatever) command. */
static int
run_command (const char *disk)
{
  FILE *fp;
  CLEANUP_FREE char *cmd = NULL;
  size_t len = 0;
  int r;
  struct var *v;

  fp = open_memstream (&cmd, &len);
  if (fp == NULL) {
    nbdkit_error ("open_memstream: %m");
    return -1;
  }

  /* Avoid stdin/stdout leaking (because of nbdkit -s). */
  fprintf (fp, "exec </dev/null >/dev/null\n");

  /* Set the standard shell variables. */
  fprintf (fp, "disk=");
  shell_quote (disk, fp);
  putc ('\n', fp);
  fprintf (fp, "size=%" PRIi64 "\n", requested_size);
  putc ('\n', fp);

  /* The other parameters/shell variables. */
  for (v = vars; v != NULL; v = v->next) {
    /* Keys probably can never contain shell-unsafe chars (because of
     * nbdkit's own restrictions), but quoting it makes it safe.
     */
    shell_quote (v->key, fp);
    putc ('=', fp);
    shell_quote (v->value, fp);
    putc ('\n', fp);
  }
  putc ('\n', fp);

  /* The command. */
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

/* For block devices, stat->st_size is not the true size. */
static int64_t
block_device_size (int fd)
{
  off_t size;

  size = lseek (fd, 0, SEEK_END);
  if (size == -1) {
    nbdkit_error ("lseek: %m");
    return -1;
  }

  return size;
}

static void *
tmpdisk_open (int readonly)
{
  struct handle *h;
  CLEANUP_FREE char *dir = NULL, *disk = NULL;
  int flags;
  struct stat statbuf;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    goto error;
  }
  h->fd = -1;
  h->size = -1;
  h->can_punch_hole = true;

  /* For security reasons we have to create a temporary directory
   * under tmpdir that only the current user can access.  If we
   * created it in a shared directory then another user might be able
   * to see the temporary file being created and interfere with it
   * before we reopen it in the plugin below.
   */
  if (asprintf (&dir, "%s/tmpdiskXXXXXX", tmpdir) == -1) {
    nbdkit_error ("asprintf: %m");
    goto error;
  }
  if (mkdtemp (dir) == NULL) {
    nbdkit_error ("%s: %m", dir);
    goto error;
  }
  if (asprintf (&disk, "%s/disk", dir) == -1) {
    nbdkit_error ("asprintf: %m");
    goto error;
  }

  /* Now run the mkfs command. */
  if (run_command (disk) == -1)
    goto error;

  /* The external command must have created the disk, and then we must
   * find the true size.
   */
  if (readonly)
    flags = O_RDONLY | O_CLOEXEC;
  else
    flags = O_RDWR | O_CLOEXEC;
  h->fd = open (disk, flags);
  if (h->fd == -1) {
    nbdkit_error ("open: %s: %m", disk);
    goto error;
  }

  if (fstat (h->fd, &statbuf) == -1) {
    nbdkit_error ("fstat: %s: %m", disk);
    goto error;
  }

  /* The command could set $disk to a regular file or a block device
   * (or a symlink to either), so we must check that here.
   */
  if (S_ISBLK (statbuf.st_mode)) {
    h->size = block_device_size (h->fd);
    if (h->size == -1)
      goto error;
  }
  else                          /* Regular file. */
    h->size = statbuf.st_size;
  nbdkit_debug ("tmpdisk: requested_size = %" PRIi64 ", size = %" PRIi64,
                requested_size, h->size);

  /* We don't need the disk to appear in the filesystem since we hold
   * a file descriptor and access it through that, so unlink the disk.
   * This also ensures it is always cleaned up.
   */
  unlink (disk);
  rmdir (dir);

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
  .unload            = tmpdisk_unload,
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
