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
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <nbdkit-plugin.h>

#include "regions.h"

#include "virtual-floppy.h"

/* Directory. */
static char *dir = NULL;

/* Volume label. */
static const char *label = "NBDKITFLOPY";

/* Filesystem size. */
static uint64_t size = 0;

/* Virtual floppy. */
static struct virtual_floppy floppy;

static void
floppy_load (void)
{
  init_virtual_floppy (&floppy);
}

static void
floppy_unload (void)
{
  free (dir);
  free_virtual_floppy (&floppy);
}

static int
floppy_config (const char *key, const char *value)
{
  int64_t r;

  if (strcmp (key, "dir") == 0) {
    if (dir != NULL) {
      /* TODO: Support merging of multiple directories, like iso plugin. */
      nbdkit_error ("dir=<DIRECTORY> must only be set once");
      return -1;
    }
    dir = nbdkit_realpath (value);
    if (dir == NULL)
      return -1;
  }
  else if (strcmp (key, "label") == 0) {
    label = value;
  }
  else if (strcmp (key, "size") == 0) {
    r = nbdkit_parse_size (value);
    if (r == -1)
      return -1;
    size = r;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int
floppy_config_complete (void)
{
  if (dir == NULL) {
    nbdkit_error ("you must supply the dir=<DIRECTORY> parameter "
                  "after the plugin name on the command line");
    return -1;
  }

  return 0;
}

#define floppy_config_help \
  "dir=<DIRECTORY>     (required) The directory to serve.\n" \
  "label=<LABEL>                  The volume label." \

static int
floppy_get_ready (void)
{
  return create_virtual_floppy (dir, label, size, &floppy);
}

static void *
floppy_open (int readonly)
{
  return NBDKIT_HANDLE_NOT_NEEDED;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Get the file size. */
static int64_t
floppy_get_size (void *handle)
{
  return virtual_size (&floppy.regions);
}

/* Serves the same data over multiple connections. */
static int
floppy_can_multi_conn (void *handle)
{
  return 1;
}

/* Cache. */
static int
floppy_can_cache (void *handle)
{
  /* Let nbdkit call pread to populate the file system cache. */
  return NBDKIT_CACHE_EMULATE;
}

/* Read data from the file. */
static int
floppy_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  while (count > 0) {
    const struct region *region = find_region (&floppy.regions, offset);
    size_t i, len;
    const char *host_path;
    int fd;
    ssize_t r;

    /* Length to end of region. */
    len = region->end - offset + 1;
    if (len > count)
      len = count;

    switch (region->type) {
    case region_file:
      i = region->u.i;
      assert (i < floppy.files.len);
      host_path = floppy.files.ptr[i].host_path;
      fd = open (host_path, O_RDONLY|O_CLOEXEC);
      if (fd == -1) {
        nbdkit_error ("open: %s: %m", host_path);
        return -1;
      }
      r = pread (fd, buf, len, offset - region->start);
      if (r == -1) {
        nbdkit_error ("pread: %s: %m", host_path);
        close (fd);
        return -1;
      }
      if (r == 0) {
        nbdkit_error ("pread: %s: unexpected end of file", host_path);
        close (fd);
        return -1;
      }
      close (fd);
      len = r;
      break;

    case region_data:
      memcpy (buf, &region->u.data[offset - region->start], len);
      break;

    case region_zero:
      memset (buf, 0, len);
      break;
    }

    count -= len;
    buf += len;
    offset += len;
  }

  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "floppy",
  .longname          = "nbdkit floppy plugin",
  .version           = PACKAGE_VERSION,
  .load              = floppy_load,
  .unload            = floppy_unload,
  .config            = floppy_config,
  .config_complete   = floppy_config_complete,
  .config_help       = floppy_config_help,
  .magic_config_key  = "dir",
  .get_ready         = floppy_get_ready,
  .open              = floppy_open,
  .get_size          = floppy_get_size,
  .can_multi_conn    = floppy_can_multi_conn,
  .can_cache         = floppy_can_cache,
  .pread             = floppy_pread,
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
