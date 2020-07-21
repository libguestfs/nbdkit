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
#include <inttypes.h>
#include <errno.h>

/* Inlining is broken in the ext2fs header file.  Disable it by
 * defining the following:
 */
#define NO_INLINE_FUNCS
#include <ext2fs.h>

#define NBDKIT_API_VERSION 2

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "io.h"

/* Filename parameter, or NULL to honor export name. */
static char *file;

static void
ext2_load (void)
{
  initialize_ext2_error_table ();
}

static void
ext2_unload (void)
{
  free (file);
}

static int
ext2_config (nbdkit_next_config *next, void *nxdata,
             const char *key, const char *value)
{
  if (strcmp (key, "ext2file") == 0) {
    if (file != NULL) {
      nbdkit_error ("ext2file parameter specified more than once");
      return -1;
    }
    file = strdup (value);
    if (file == NULL) {
      nbdkit_error ("strdup: %m");
      return -1;
    }
    return 0;
  }
  else
    return next (nxdata, key, value);
}

static int
ext2_config_complete (nbdkit_next_config_complete *next, void *nxdata)
{
  if (file == NULL) {
    nbdkit_error ("you must supply ext2file=<FILE> parameter "
                  "after the plugin name on the command line");
    return -1;
  }

  if (strcmp (file, "exportname") == 0) {
    free (file);
    file = NULL;
  }
  else if (file[0] != '/') {
    nbdkit_error ("the file parameter must refer to an absolute path");
    return -1;
  }

  return next (nxdata);
}

#define ext2_config_help \
  "ext2file=<FILENAME>  (required) Absolute name of file to serve inside the\n" \
  "                     disk image, or 'exportname' for client choice."

/* The per-connection handle. */
struct handle {
  char *exportname;             /* Client export name. */
  ext2_filsys fs;               /* Filesystem handle. */
  ext2_ino_t ino;               /* Inode of open file. */
  ext2_file_t file;             /* File handle. */
  struct nbdkit_next next;      /* "name" parameter to ext2fs_open. */
};

/* Create the per-connection handle. */
static void *
ext2_open (nbdkit_next_open *next, void *nxdata,
           int readonly, const char *exportname)
{
  struct handle *h;

  h = calloc (1, sizeof *h);
  if (h == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }

  /* Save the client exportname in the handle. */
  h->exportname = strdup (exportname);
  if (h->exportname == NULL) {
    nbdkit_error ("strdup: %m");
    free (h);
    return NULL;
  }

  /* If file == NULL (ie. using exportname) then don't
   * pass the client exportname to the lower layers.
   */
  exportname = file ? exportname : "";

  /* Request write access to the underlying plugin, for journal replay. */
  if (next (nxdata, 0, exportname) == -1) {
    free (h->exportname);
    free (h);
    return NULL;
  }

  return h;
}

static int
ext2_prepare (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle,
              int readonly)
{
  struct handle *h = handle;
  errcode_t err;
  int fs_flags;
  int file_flags;
  struct ext2_inode inode;
  int64_t r;
  CLEANUP_FREE char *name = NULL;
  const char *fname = file ?: h->exportname;
  CLEANUP_FREE char *absname = NULL;

  fs_flags = 0;
#ifdef EXT2_FLAG_64BITS
  fs_flags |= EXT2_FLAG_64BITS;
#endif
  r = next_ops->get_size (nxdata);
  if (r == -1)
    return -1;
  r = next_ops->can_write (nxdata);
  if (r == -1)
    return -1;
  if (r == 0)
    readonly = 1;

  if (!readonly)
    fs_flags |= EXT2_FLAG_RW;

  h->next.next_ops = next_ops;
  h->next.nxdata = nxdata;
  name = nbdkit_io_encode (&h->next);
  if (!name) {
    nbdkit_error ("nbdkit_io_encode: %m");
    return -1;
  }

  if (fname[0] != '/') {
    if (asprintf (&absname, "/%s", fname) < 0) {
      nbdkit_error ("asprintf: %m");
      return -1;
    }
    fname = absname;
  }

  err = ext2fs_open (name, fs_flags, 0, 0, nbdkit_io_manager, &h->fs);
  if (err != 0) {
    nbdkit_error ("open: %s", error_message (err));
    goto err0;
  }

  if (strcmp (fname, "/") == 0)
    /* probably gonna fail, but we'll catch it later */
    h->ino = EXT2_ROOT_INO;
  else {
    err = ext2fs_namei (h->fs, EXT2_ROOT_INO, EXT2_ROOT_INO,
                        &fname[1], &h->ino);
    if (err != 0) {
      nbdkit_error ("%s: namei: %s", fname, error_message (err));
      goto err1;
    }
  }

  /* Check that fname is a regular file.
   * XXX This won't follow symlinks, we'd have to do that manually.
   */
  err = ext2fs_read_inode (h->fs, h->ino, &inode);
  if (err != 0) {
    nbdkit_error ("%s: inode: %s", fname, error_message (err));
    goto err1;
  }
  if (!LINUX_S_ISREG (inode.i_mode)) {
    nbdkit_error ("%s: must be a regular file in the disk image", fname);
    goto err1;
  }

  file_flags = 0;
  if (!readonly)
    file_flags |= EXT2_FILE_WRITE;
  err = ext2fs_file_open2 (h->fs, h->ino, NULL, file_flags, &h->file);
  if (err != 0) {
    nbdkit_error ("%s: open: %s", fname, error_message (err));
    goto err1;
  }

  return 0;

 err1:
  ext2fs_close (h->fs);
  h->fs = NULL;
 err0:
  return -1;
}

/* Free up the per-connection handle. */
static void
ext2_close (void *handle)
{
  struct handle *h = handle;

  if (h->fs) {
    ext2fs_file_close (h->file);
    ext2fs_close (h->fs);
  }
  free (h->exportname);
  free (h);
}

static int
ext2_can_fua (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  return NBDKIT_FUA_NATIVE;
}

static int
ext2_can_cache (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  /* Let nbdkit call pread to populate the file system cache. */
  return NBDKIT_CACHE_EMULATE;
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
static int ext2_thread_model (void)
{
  return NBDKIT_THREAD_MODEL_SERIALIZE_CONNECTIONS;
}

/* Get the disk size. */
static int64_t
ext2_get_size (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  struct handle *h = handle;
  errcode_t err;
  uint64_t size;

  err = ext2fs_file_get_lsize (h->file, (__u64 *) &size);
  if (err != 0) {
    nbdkit_error ("%s: lsize: %s", file, error_message (err));
    return -1;
  }
  return (int64_t) size;
}

/* Read data. */
static int
ext2_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle, void *buf, uint32_t count, uint64_t offset,
            uint32_t flags, int *errp)
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
      nbdkit_error ("%s: llseek: %s", file, error_message (err));
      *errp = errno;
      return -1;
    }

    err = ext2fs_file_read (h->file, buf, (unsigned int) count, &got);
    if (err != 0) {
      nbdkit_error ("%s: read: %s", file, error_message (err));
      *errp = errno;
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
ext2_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle, const void *buf, uint32_t count, uint64_t offset,
             uint32_t flags, int *errp)
{
  struct handle *h = handle;
  errcode_t err;
  unsigned int written;

  while (count > 0) {
    err = ext2fs_file_llseek (h->file, offset, EXT2_SEEK_SET, NULL);
    if (err != 0) {
      nbdkit_error ("%s: llseek: %s", file, error_message (err));
      *errp = errno;
      return -1;
    }

    err = ext2fs_file_write (h->file, buf, (unsigned int) count, &written);
    if (err != 0) {
      nbdkit_error ("%s: write: %s", file, error_message (err));
      *errp = errno;
      return -1;
    }

    buf += written;
    count -= written;
    offset += written;
  }

  if ((flags & NBDKIT_FLAG_FUA) != 0) {
    err = ext2fs_file_flush (h->file);
    if (err != 0) {
      nbdkit_error ("%s: flush: %s", file, error_message (err));
      *errp = errno;
      return -1;
    }
  }

  return 0;
}

static int
ext2_flush (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle, uint32_t flags, int *errp)
{
  struct handle *h = handle;
  errcode_t err;

  err = ext2fs_file_flush (h->file);
  if (err != 0) {
    nbdkit_error ("%s: flush: %s", file, error_message (err));
    *errp = errno;
    return -1;
  }

  return 0;
}

/* XXX It seems as if we should be able to support trim and zero, if
 * we could work out how those are implemented in the ext2fs API which
 * is very obscure.
 */

static struct nbdkit_filter filter = {
  .name              = "ext2",
  .longname          = "nbdkit ext2 filter",
  .load              = ext2_load,
  .unload            = ext2_unload,
  .config            = ext2_config,
  .config_complete   = ext2_config_complete,
  .config_help       = ext2_config_help,
  .thread_model      = ext2_thread_model,
  .open              = ext2_open,
  .prepare           = ext2_prepare,
  .close             = ext2_close,
  .can_fua           = ext2_can_fua,
  .can_cache         = ext2_can_cache,
  .get_size          = ext2_get_size,
  .pread             = ext2_pread,
  .pwrite            = ext2_pwrite,
  .flush             = ext2_flush,
};

NBDKIT_REGISTER_FILTER(filter)
