/* nbdkit
 * Copyright Red Hat
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
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define NBDKIT_API_VERSION 2

#include <nbdkit-plugin.h>

#include "random.h"
#include "regions.h"

#include "virtual-disk.h"

/* Directory, label, type, size parameters. */
const char *dir;
const char *label;
const char *type = "ext2";
int64_t size;
bool size_add_estimate;  /* if size=+SIZE was used */

/* Virtual disk. */
static struct virtual_disk disk;

/* Used to create a random GUID for the partition. */
struct random_state random_state;

static void
linuxdisk_load (void)
{
  init_virtual_disk (&disk);
  xsrandom (time (NULL), &random_state);
}

static void
linuxdisk_unload (void)
{
  free_virtual_disk (&disk);
}

static int
linuxdisk_config (const char *key, const char *value)
{
  if (strcmp (key, "dir") == 0) {
    if (dir != NULL) {
      /* TODO: Support merging of multiple directories, like iso plugin. */
      nbdkit_error ("dir=<DIRECTORY> must only be set once");
      return -1;
    }
    /* We don't actually need to use realpath here because the
     * directory is only used in .get_ready, before we chdir.  Not
     * doing realpath is helpful because on Windows it will munge the
     * path in such a way that external mke2fs cannot parse it.
     */
    dir = value;
  }
  else if (strcmp (key, "label") == 0) {
    label = value;
  }
  else if (strcmp (key, "type") == 0) {
    if (strncmp (value, "ext", 3) != 0) {
      nbdkit_error ("type=<TYPE> must be an filesystem type "
                    "supported by e2fsprogs");
      return -1;
    }
    type = value;
  }
  else if (strcmp (key, "size") == 0) {
    if (value[0] == '+') {
      size_add_estimate = true;
      value++;
    }
    else
      size_add_estimate = false;
    size = nbdkit_parse_size (value);
    if (size == -1)
      return -1;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int
linuxdisk_config_complete (void)
{
  if (dir == NULL) {
    nbdkit_error ("you must supply the dir=<DIRECTORY> parameter "
                  "after the plugin name on the command line");
    return -1;
  }

  return 0;
}

#define linuxdisk_config_help \
  "dir=<DIRECTORY>  (required) The directory to serve.\n" \
  "label=<LABEL>               The filesystem label.\n" \
  "type=ext2|ext3|ext4         The filesystem type.\n" \
  "size=[+]<SIZE>              The virtual filesystem size."

static int
linuxdisk_get_ready (void)
{
  return create_virtual_disk (&disk);
}

static void *
linuxdisk_open (int readonly)
{
  return NBDKIT_HANDLE_NOT_NEEDED;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Get the file size. */
static int64_t
linuxdisk_get_size (void *handle)
{
  return virtual_size (&disk.regions);
}

/* Serves the same data over multiple connections. */
static int
linuxdisk_can_multi_conn (void *handle)
{
  return 1;
}

/* Cache. */
static int
linuxdisk_can_cache (void *handle)
{
  /* Let nbdkit call pread to populate the file system cache. */
  return NBDKIT_CACHE_EMULATE;
}

/* Read data from the virtual disk. */
static int
linuxdisk_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
                 uint32_t flags)
{
  while (count > 0) {
    const struct region *region = find_region (&disk.regions, offset);
    size_t len;
    ssize_t r;

    /* Length to end of region. */
    len = region->end - offset + 1;
    if (len > count)
      len = count;

    switch (region->type) {
    case region_file:
      /* We don't use region->u.i since there is only one backing
       * file, and we have that open already (in ‘disk.fd’).
       */
      r = pread (disk.fd, buf, len, offset - region->start);
      if (r == -1) {
        nbdkit_error ("pread: %m");
        return -1;
      }
      if (r == 0) {
        nbdkit_error ("pread: unexpected end of file");
        return -1;
      }
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
  .name              = "linuxdisk",
  .longname          = "nbdkit Linux virtual disk plugin",
  .version           = PACKAGE_VERSION,
  .load              = linuxdisk_load,
  .unload            = linuxdisk_unload,
  .config            = linuxdisk_config,
  .config_complete   = linuxdisk_config_complete,
  .config_help       = linuxdisk_config_help,
  .magic_config_key  = "dir",
  .get_ready         = linuxdisk_get_ready,
  .open              = linuxdisk_open,
  .get_size          = linuxdisk_get_size,
  .can_multi_conn    = linuxdisk_can_multi_conn,
  .can_cache         = linuxdisk_can_cache,
  .pread             = linuxdisk_pread,
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN (plugin)
