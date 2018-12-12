/* nbdkit
 * Copyright (C) 2017-2018 Red Hat Inc.
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
#include <inttypes.h>

/* Inlining is broken in the ext2fs header file.  Disable it by
 * defining the following:
 */
#define NO_INLINE_FUNCS
#include <ext2fs.h>

#define NBDKIT_API_VERSION 2

#include <nbdkit-plugin.h>

/* Disk image and filename parameters. */
static char *disk;
static char *file;

static void
ext2_load (void)
{
  initialize_ext2_error_table ();
}

static void
ext2_unload (void)
{
  free (disk);
  free (file);
}

static int
ext2_config (const char *key, const char *value)
{
  if (strcmp (key, "disk") == 0) {
    if (disk != NULL) {
      nbdkit_error ("disk parameter specified more than once");
      return -1;
    }
    disk = nbdkit_absolute_path (value);
    if (disk == NULL)
      return -1;
  }
  else if (strcmp (key, "file") == 0) {
    if (file != NULL) {
      nbdkit_error ("file parameter specified more than once");
      return -1;
    }
    file = strdup (value);
    if (file == NULL) {
      nbdkit_error ("strdup: %m");
      return -1;
    }
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int
ext2_config_complete (void)
{
  if (disk == NULL || file == NULL) {
    nbdkit_error ("you must supply disk=<DISK> and file=<FILE> parameters "
                  "after the plugin name on the command line");
    return -1;
  }

  if (file[0] != '/') {
    nbdkit_error ("the file parameter must refer to an absolute path");
    return -1;
  }

  return 0;
}

#define ext2_config_help \
  "disk=<FILENAME>  (required) Raw ext2, ext3 or ext4 filesystem.\n" \
  "file=<FILENAME>  (required) File to serve inside the disk image."

/* The per-connection handle. */
struct handle {
  ext2_filsys fs;               /* Filesystem handle. */
  ext2_ino_t ino;               /* Inode of open file. */
  ext2_file_t file;             /* File handle. */
};

/* Create the per-connection handle. */
static void *
ext2_open (int readonly)
{
  struct handle *h;
  errcode_t err;
  int fs_flags;
  int file_flags;
  struct ext2_inode inode;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  fs_flags = 0;
#ifdef EXT2_FLAG_64BITS
  fs_flags |= EXT2_FLAG_64BITS;
#endif
  if (!readonly)
    fs_flags |= EXT2_FLAG_RW;

  err = ext2fs_open (disk, fs_flags, 0, 0, unix_io_manager, &h->fs);
  if (err != 0) {
    nbdkit_error ("%s: open: %s", disk, error_message (err));
    goto err0;
  }

  if (strcmp (file, "/") == 0)
    /* probably gonna fail, but we'll catch it later */
    h->ino = EXT2_ROOT_INO;
  else {
    err = ext2fs_namei (h->fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
                        &file[1], &h->ino);
    if (err != 0) {
      nbdkit_error ("%s: %s: namei: %s", disk, file, error_message (err));
      goto err1;
    }
  }

  /* Check the file is a regular file.
   * XXX This won't follow symlinks, we'd have to do that manually.
   */
  err = ext2fs_read_inode (h->fs, h->ino, &inode);
  if (err != 0) {
    nbdkit_error ("%s: %s: inode: %s", disk, file, error_message (err));
    goto err1;
  }
  if (!LINUX_S_ISREG (inode.i_mode)) {
    nbdkit_error ("%s: %s: must be a regular file in the disk image",
                  disk, file);
    goto err1;
  }

  file_flags = 0;
  if (!readonly)
    file_flags |= EXT2_FILE_WRITE;
  err = ext2fs_file_open2 (h->fs, h->ino, NULL, file_flags, &h->file);
  if (err != 0) {
    nbdkit_error ("%s: %s: open: %s", disk, file, error_message (err));
    goto err1;
  }

  return h;

 err1:
  ext2fs_close (h->fs);
 err0:
  free (h);
  return NULL;
}

/* Free up the per-connection handle. */
static void
ext2_close (void *handle)
{
  struct handle *h = handle;

  ext2fs_file_close (h->file);
  ext2fs_close (h->fs);
  free (h);
}

static int
ext2_can_fua (void *handle)
{
  return NBDKIT_FUA_NATIVE;
}

/* It might be possible to relax this, but it's complicated.
 *
 * It's desirable for ‘nbdkit -r’ to behave the same way as
 * ‘mount -o ro’.  But we don't know the state of the readonly flag
 * until ext2_open is called (because the NBD client can also request
 * a readonly connection).  So we could not set the "ro" flag if we
 * opened the filesystem any earlier (eg in ext2_config).
 *
 * So out of necessity we have one ext2_filsys handle per connection,
 * but if we allowed parallel work on those handles then we would get
 * data corruption, so we need to serialize connections.
 */
#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_CONNECTIONS

/* Get the disk size. */
static int64_t
ext2_get_size (void *handle)
{
  struct handle *h = handle;
  errcode_t err;
  uint64_t size;

  err = ext2fs_file_get_lsize (h->file, (__u64 *) &size);
  if (err != 0) {
    nbdkit_error ("%s: %s: lsize: %s", disk, file, error_message (err));
    return -1;
  }
  return (int64_t) size;
}

/* Read data. */
static int
ext2_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
            uint32_t flags)
{
  struct handle *h = handle;
  errcode_t err;
  unsigned int got;

  while (count > 0) {
    /* Although this function weirdly can return the new offset,
     * examination of the code shows that it never returns anything
     * different from what we set, so NULL out that parameter.
     */
    err = ext2fs_file_llseek (h->file, offset, EXT2_SEEK_SET, NULL);
    if (err != 0) {
      nbdkit_error ("%s: %s: llseek: %s", disk, file, error_message (err));
      return -1;
    }

    err = ext2fs_file_read (h->file, buf, (unsigned int) count, &got);
    if (err != 0) {
      nbdkit_error ("%s: %s: read: %s", disk, file, error_message (err));
      return -1;
    }

    buf += got;
    count -= got;
    offset += got;
  }

  return 0;
}

/* Write data to the file. */
static int
ext2_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
             uint32_t flags)
{
  struct handle *h = handle;
  errcode_t err;
  unsigned int written;

  while (count > 0) {
    err = ext2fs_file_llseek (h->file, offset, EXT2_SEEK_SET, NULL);
    if (err != 0) {
      nbdkit_error ("%s: %s: llseek: %s", disk, file, error_message (err));
      return -1;
    }

    err = ext2fs_file_write (h->file, buf, (unsigned int) count, &written);
    if (err != 0) {
      nbdkit_error ("%s: %s: write: %s", disk, file, error_message (err));
      return -1;
    }

    buf += written;
    count -= written;
    offset += written;
  }

  if ((flags & NBDKIT_FLAG_FUA) != 0) {
    err = ext2fs_file_flush (h->file);
    if (err != 0) {
      nbdkit_error ("%s: %s: flush: %s", disk, file, error_message (err));
      return -1;
    }
  }

  return 0;
}

static int
ext2_flush (void *handle, uint32_t flags)
{
  struct handle *h = handle;
  errcode_t err;

  err = ext2fs_file_flush (h->file);
  if (err != 0) {
    nbdkit_error ("%s: %s: flush: %s", disk, file, error_message (err));
    return -1;
  }

  return 0;
}

/* XXX It seems as if we should be able to support trim and zero, if
 * we could work out how those are implemented in the ext2fs API which
 * is very obscure.
 */

static struct nbdkit_plugin plugin = {
  .name              = "ext2",
  .version           = PACKAGE_VERSION,
  .load              = ext2_load,
  .unload            = ext2_unload,
  .config            = ext2_config,
  .config_complete   = ext2_config_complete,
  .config_help       = ext2_config_help,
  .open              = ext2_open,
  .close             = ext2_close,
  .can_fua           = ext2_can_fua,
  .get_size          = ext2_get_size,
  .pread             = ext2_pread,
  .pwrite            = ext2_pwrite,
  .flush             = ext2_flush,
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
