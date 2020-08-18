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
#include <dirent.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <pthread.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "fdatasync.h"
#include "utils.h"

static char *dir;                   /* dir parameter */
static DIR *exportsdir;             /* opened exports dir */
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
ondemand_unload (void)
{
  struct var *v, *v_next;

  for (v = vars; v != NULL; v = v_next) {
    v_next = v->next;
    free (v);
  }

  if (exportsdir)
    closedir (exportsdir);
  free (dir);
}

static int
ondemand_config (const char *key, const char *value)
{
  if (strcmp (key, "command") == 0) {
    command = value;
  }
  else if (strcmp (key, "size") == 0) {
    requested_size = nbdkit_parse_size (value);
    if (requested_size == -1)
      return -1;
  }
  else if (strcmp (key, "dir") == 0) {
    dir = nbdkit_realpath (value);
    if (dir == NULL)
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
ondemand_config_complete (void)
{
  if (dir == NULL || requested_size == -1) {
    nbdkit_error ("dir and size parameters are required");
    return -1;
  }

  return 0;
}

static int
ondemand_get_ready (void)
{
  exportsdir = opendir (dir);
  if (exportsdir == NULL) {
    nbdkit_error ("opendir: %s: %m", dir);
    return -1;
  }

  return 0;
}

#define ondemand_config_help \
  "dir=<EXPORTSDIR> (required) Directory containing filesystems.\n" \
  "size=<SIZE>      (required) Virtual filesystem size.\n" \
  "label=<LABEL>               The filesystem label.\n" \
  "type=ext4|...               The filesystem type.\n" \
  "command=<COMMAND>           Alternate command instead of mkfs."

/* Because we rewind the exportsdir handle, we need a lock to protect
 * list_exports from being called in parallel.
 */
static pthread_mutex_t exports_lock = PTHREAD_MUTEX_INITIALIZER;

static int
ondemand_list_exports (int readonly, int default_only,
                       struct nbdkit_exports *exports)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&exports_lock);
  struct dirent *d;

  /* First entry should be the default export.  XXX Should we check if
   * the "default" file was created?  I don't think we need to.
   */
  if (nbdkit_add_export (exports, "", NULL) == -1)
    return -1;
  if (default_only) return 0;

  /* Read the rest of the exports. */
  rewinddir (exportsdir);

  /* XXX Output is not sorted.  Does it matter? */
  while (errno = 0, (d = readdir (exportsdir)) != NULL) {
    /* Skip any file containing non-permitted characters '.' and ':'.
     * As a side effect this skips all dot-files.  Commands can use
     * dot-files to "hide" files in the export dir (eg. if needing to
     * keep state).
     */
    if (strchr (d->d_name, '.') || strchr (d->d_name, ':'))
      continue;

    /* Skip the "default" filename which refers to the "" export. */
    if (strcmp (d->d_name, "default") == 0)
      continue;

    if (nbdkit_add_export (exports, d->d_name, NULL) == -1)
      return -1;
  }

  /* Did readdir fail? */
  if (errno != 0) {
    nbdkit_error ("readdir: %s: %m", dir);
    return -1;
  }

  return 0;
}

struct handle {
  int fd;
  int64_t size;
  const char *exportname;
  bool can_punch_hole;
};

/* In theory clients that want multi-conn should all pass the same
 * export name, and that would be safe.  However our locking
 * implementation (see ondemand_open) does not allow this.  It seems
 * to work around this we will need to implement client UUID in the
 * protocol.  (https://lists.debian.org/nbd/2020/08/msg00001.html)
 */
static int
ondemand_can_multi_conn (void *handle)
{
  return 0;
}

static int
ondemand_can_trim (void *handle)
{
#ifdef FALLOC_FL_PUNCH_HOLE
  return 1;
#else
  return 0;
#endif
}

static int
ondemand_can_fua (void *handle)
{
  return NBDKIT_FUA_NATIVE;
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
ondemand_open (int readonly)
{
  struct handle *h;
  CLEANUP_FREE char *disk = NULL;
  int flags, err;
  struct stat statbuf;
  struct flock lock;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    goto error;
  }
  h->fd = -1;
  h->size = -1;
  h->can_punch_hole = true;

  /* This is safe since we're only storing it in the handle, so only
   * for the lifetime of this connection.
   */
  h->exportname = nbdkit_export_name ();
  if (!h->exportname) {
    nbdkit_error ("internal error: expected nbdkit_export_name () != NULL");
    goto error;
  }
  if (strcmp (h->exportname, "") == 0)
    h->exportname = "default";

  /* Verify that the export name is valid. */
  if (strlen (h->exportname) > NAME_MAX ||
      strchr (h->exportname, '.') ||
      strchr (h->exportname, '/') ||
      strchr (h->exportname, ':')) {
    nbdkit_error ("invalid exportname ‘%s’ rejected", h->exportname);
    goto error;
  }

  /* Try to open the filesystem. */
  if (readonly)
    flags = O_RDONLY | O_CLOEXEC;
  else
    flags = O_RDWR | O_CLOEXEC;
  h->fd = openat (dirfd (exportsdir), h->exportname, flags);
  if (h->fd == -1) {
    if (errno != ENOENT) {
      nbdkit_error ("open: %s/%s: %m", dir, h->exportname);
      goto error;
    }

    /* Create the filesystem. */
    if (asprintf (&disk, "%s/%s", dir, h->exportname) == -1) {
      nbdkit_error ("asprintf: %m");
      goto error;
    }

    /* Now run the mkfs command. */
    if (run_command (disk) == -1)
      goto error;

    h->fd = openat (dirfd (exportsdir), h->exportname, flags);
    if (h->fd == -1) {
      nbdkit_error ("open: %s/%s: %m", dir, h->exportname);
      goto error;
    }
  }

  /* Lock the file to prevent filesystem corruption.  It's safe for
   * all clients to be reading.  If a client wants to write it must
   * have exclusive access.
   *
   * This uses a currently Linux-specific extension.  It requires
   * Linux >= 3.15 (released in 2014, later backported to RHEL 7).
   * There is no sensible way to do this in pure POSIX.
   */
#ifdef F_OFD_SETLK
  memset (&lock, 0, sizeof lock);
  if (readonly)
    lock.l_type = F_RDLCK;
  else
    lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  lock.l_start = 0;
  lock.l_len = 0;
  if (fcntl (h->fd, F_OFD_SETLK, &lock) == -1) {
    if (errno == EACCES || errno == EAGAIN) {
      nbdkit_error ("%s: filesystem is locked by another client",
                    h->exportname);
      /* XXX Would be nice if NBD protocol supported some kind of "is
       * locked" indication.  If it did we could use it here.
       */
      errno = EINVAL;
      goto error;
    }
    else {
      nbdkit_error ("fcntl: %s/%s: %m", dir, h->exportname);
      goto error;
    }
  }
#endif

  /* Find the size of the disk. */
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
  nbdkit_debug ("ondemand: requested_size = %" PRIi64 ", size = %" PRIi64,
                requested_size, h->size);

  /* Return the handle. */
  return h;

 error:
  err = errno;
  if (h) {
    if (h->fd >= 0)
      close (h->fd);
    free (h);
  }
  errno = err;
  return NULL;
}

static void
ondemand_close (void *handle)
{
  struct handle *h = handle;

  close (h->fd);
  free (h);
}

static int64_t
ondemand_get_size (void *handle)
{
  struct handle *h = handle;

  return h->size;
}

/* Read data from the file. */
static int
ondemand_pread (void *handle, void *buf,
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

/* Flush the file to disk. */
static int
ondemand_flush (void *handle, uint32_t flags)
{
  struct handle *h = handle;

  if (fdatasync (h->fd) == -1) {
    nbdkit_error ("fdatasync: %m");
    return -1;
  }

  return 0;
}

/* Write data to the file. */
static int
ondemand_pwrite (void *handle, const void *buf,
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

  if ((flags & NBDKIT_FLAG_FUA) && ondemand_flush (handle, 0) == -1)
    return -1;

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
ondemand_trim (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
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

  if ((flags & NBDKIT_FLAG_FUA) && ondemand_flush (handle, 0) == -1)
    return -1;

  return 0;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

static struct nbdkit_plugin plugin = {
  .name              = "ondemand",
  .version           = PACKAGE_VERSION,

  .unload            = ondemand_unload,
  .config            = ondemand_config,
  .config_complete   = ondemand_config_complete,
  .config_help       = ondemand_config_help,
  .magic_config_key  = "size",
  .get_ready         = ondemand_get_ready,

  .list_exports      = ondemand_list_exports,

  .can_multi_conn    = ondemand_can_multi_conn,
  .can_trim          = ondemand_can_trim,
  .can_fua           = ondemand_can_fua,
  .get_size          = ondemand_get_size,

  .open              = ondemand_open,
  .close             = ondemand_close,
  .pread             = ondemand_pread,
  .pwrite            = ondemand_pwrite,
  .flush             = ondemand_flush,
  .trim              = ondemand_trim,

  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
