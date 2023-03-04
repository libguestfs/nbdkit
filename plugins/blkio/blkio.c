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

#include <blkio.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "array-size.h"
#include "const-string-vector.h"
#include "vector.h"

#define MAX_BOUNCE_BUFFER (64 * 1024 * 1024)

/* libblkio could do parallel, but we would need to reimplement this
 * plugin to use the libblkio event model.
 */
#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_REQUESTS

struct property {
  const char *name;
  char *value;
  bool value_needs_free;
};
DEFINE_VECTOR_TYPE (properties, struct property);

static const char *driver = NULL;               /* driver name - required */
static properties props = empty_vector;         /* other command line params */
static const_string_vector get_params = empty_vector; /* get= parameters */

/* XXX Should be possible to query this from libblkio. */
static bool
is_preconnect_property (const char *name)
{
  static const char *preconnect_props[] = {
    "can-add-queues", "driver", "fd", "path", "read-only"
  };
  size_t i;

  for (i = 0; i < ARRAY_SIZE (preconnect_props); ++i)
    if (strcmp (name, preconnect_props[i]) == 0)
      return true;
  return false;
}

/* Path properties need to be rewritten using nbdkit_absolute_path. */
static bool
is_path_property (const char *name)
{
  static const char *path_props[] = {
    "path"
  };
  size_t i;

  for (i = 0; i < ARRAY_SIZE (path_props); ++i)
    if (strcmp (name, path_props[i]) == 0)
      return true;
  return false;
}

static void
bio_unload (void)
{
  size_t i;

  for (i = 0; i < props.len; ++i)
    if (props.ptr[i].value_needs_free)
      free (props.ptr[i].value);
  properties_reset (&props);

  const_string_vector_reset (&get_params);
}

/* Called for each key=value passed on the command line. */
static int
bio_config (const char *key, const char *value)
{
  if (strcmp (key, "driver") == 0) {
    if (driver != NULL) {
      nbdkit_error ("'driver' property set more than once");
      return -1;
    }
    driver = value;
  }
  else if (strcmp (key, "get") == 0) {
    if (const_string_vector_append (&get_params, value) == -1)
      return -1;
  }
  else if (strcmp (key, "read-only") == 0) {
    nbdkit_error ("do not set the libblkio \"read-only\" parameter, "
                  "use the nbdkit -r flag if read-only is required");
    return -1;
  }
  else if (is_path_property (key) == 0) {
    char *path = nbdkit_absolute_path (value);
    struct property prop = { .name = key, .value = path,
                             .value_needs_free = true };

    if (path == NULL || properties_append (&props, prop) == -1)
      return -1;
  }
  else /* general property */ {
    struct property prop = { .name = key, .value = (char *) value,
                             .value_needs_free = false };

    if (properties_append (&props, prop) == -1)
      return -1;
  }

  return 0;
}

/* Check the user did pass a driver parameter. */
static int
bio_config_complete (void)
{
  if (driver == NULL) {
    nbdkit_error ("you must supply the driver=<DRIVER> parameter "
                  "after the plugin name on the command line");
    return -1;
  }

  return 0;
}

#define bio_config_help \
  "driver=<DRIVER> (required) Driver name (eg. \"nvme-io_uring\").\n" \
  "PROPERTY=VALUE             Set arbitrary libblkio property.\n" \
  "get=PROPERTY               Print property name after connection."

struct handle {
  struct blkio *b;
  struct blkio_mem_region mem_region;
};

/* Create the per-connection handle. */
static void *
bio_open (int readonly)
{
  struct handle *h;
  int r;
  size_t i;
  bool b;

  h = calloc (1, sizeof *h);
  if (h == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }

  r = blkio_create (driver, &h->b);
  if (r < 0) {
    nbdkit_error ("blkio_create: error opening driver: %s: %s", driver,
                  blkio_get_error_msg ());
    goto error;
  }

  /* If the readonly flag (nbdkit -r) is set, set that property.
   * However don't change the property otherwise.  In can_write below
   * we will check the final read-only status of the device.
   *
   * XXX This doesn't work for all drivers.  Somehow the user has to
   * just "know" that a device is read-only (or not) and must set this
   * property, otherwise libblkio fails to start with error "Device is
   * read-only".
   */
  if (readonly) {
    r = blkio_set_bool (h->b, "read-only", true);
    if (r < 0) {
      nbdkit_error ("error setting property: read-only=true: %s",
                    blkio_get_error_msg ());
      goto error;
    }
  }

  /* Set the pre-connect properties. */
  for (i = 0; i < props.len; ++i) {
    const struct property *prop = &props.ptr[i];

    if (is_preconnect_property (prop->name)) {
      r = blkio_set_str (h->b, prop->name, prop->value);
      if (r < 0) {
        nbdkit_error ("error setting property: %s=%s: %s",
                      prop->name, prop->value,
                      blkio_get_error_msg ());
        goto error;
      }
    }
  }

  /* Connect. */
  r = blkio_connect (h->b);
  if (r < 0) {
    nbdkit_error ("blkio_connect: failed to connect to device: %s",
                  blkio_get_error_msg ());
    goto error;
  }

  /* Set the post-connect properties. */
  for (i = 0; i < props.len; ++i) {
    const struct property *prop = &props.ptr[i];

    if (! is_preconnect_property (prop->name)) {
      r = blkio_set_str (h->b, prop->name, prop->value);
      if (r < 0) {
        nbdkit_error ("error setting property: %s=%s: %s",
                      prop->name, prop->value,
                      blkio_get_error_msg ());
        goto error;
      }
    }
  }

  /* Start the block device. */
  r = blkio_start (h->b);
  if (r < 0) {
    nbdkit_error ("blkio_start: failed to start device: %s",
                  blkio_get_error_msg ());
    goto error;
  }

  /* Print any properties requested on the command line (get=...). */
  for (i = 0; i < get_params.len; ++i) {
    const char *name = get_params.ptr[i];
    char *value = NULL;

    if (blkio_get_str (h->b, name, &value) == 0)
      nbdkit_debug ("get %s = %s", name, value);
    else
      nbdkit_debug ("could not get property %s: %m", name);
    free (value);
  }

  /* If memory regions are required, allocate them using the
   * convenience functions.  Note we allocate one buffer per handle.
   * It is attached to the handle so blkio_destroy will remove it.
   */
  r = blkio_get_bool (h->b, "needs-mem-regions", &b);
  if (r < 0) {
    nbdkit_error ("error reading 'needs-mem-regions' property: %s",
                  blkio_get_error_msg ());
    goto error;
  }
  if (b) {
    nbdkit_debug ("driver %s requires a bounce buffer", driver);

    r = blkio_alloc_mem_region (h->b, &h->mem_region, MAX_BOUNCE_BUFFER);
    if (r < 0) {
      nbdkit_error ("blkio_alloc_mem_region: %s", blkio_get_error_msg ());
      goto error;
    }
    r = blkio_map_mem_region (h->b, &h->mem_region);
    if (r < 0) {
      nbdkit_error ("blkio_map_mem_region: %s", blkio_get_error_msg ());
      goto error;
    }
  }

  return h;

 error:
  if (h->b)
    blkio_destroy (&h->b);
  free (h);
  return NULL;
}

/* Close the handle. */
static void
bio_close (void *handle)
{
  struct handle *h = handle;

  blkio_destroy (&h->b);
  free (h);
}

/* Get the device size. */
static int64_t
bio_get_size (void *handle)
{
  struct handle *h = handle;
  int r;
  uint64_t ret;

  r = blkio_get_uint64 (h->b, "capacity", &ret);
  if (r < 0) {
    nbdkit_error ("error reading device capacity: %s",
                  blkio_get_error_msg ());
    return -1;
  }

  return ret;
}

/* Block size preferences. */
static int
bio_block_size (void *handle, uint32_t *minimum,
                uint32_t *preferred, uint32_t *maximum)
{
  struct handle *h = handle;
  int request_alignment = 0, optimal_io_alignment = 0;

  /* Don't worry if these fail.  We also assume 0 for unspecified. */
  blkio_get_int (h->b, "request-alignment", &request_alignment);
  blkio_get_int (h->b, "optimal-io-alignment", &optimal_io_alignment);

  /* Ignore unspecified or bogusly large alignments. */
  if (request_alignment <= 0 || request_alignment > 1024*1024 ||
      optimal_io_alignment < 0 ||
      optimal_io_alignment > 1024*1024) {
    *minimum = *preferred = *maximum = 0;
    return 0;
  }

  *minimum = request_alignment;
  *preferred = optimal_io_alignment;
  *maximum = 0xffffffff;
  return 0;
}

/* Find out if the connection is writable. */
static int
bio_can_write (void *handle)
{
  struct handle *h = handle;
  int r;
  bool ro = true;

  r = blkio_get_bool (h->b, "read-only", &ro);
  if (r < 0) {
    nbdkit_error ("blkio_get_bool: read-only: %s",
                  blkio_get_error_msg ());
    return -1;
  }
  return !ro;
}

/* We always support FUA natively. */
static int
bio_can_fua (void *handle)
{
  return NBDKIT_FUA_NATIVE;
}

/* Read data from the device. */
static int
bio_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
           uint32_t flags)
{
  struct handle *h = handle;
  int r;
  struct blkioq *q = blkio_get_queue (h->b, 0);
  struct blkio_completion completion;

  if (h->mem_region.addr && count > MAX_BOUNCE_BUFFER) {
    nbdkit_error ("request too large for bounce buffer");
    return -1;
  }

  blkioq_read (q, offset, h->mem_region.addr ? : buf, count, NULL, 0);
  r = blkioq_do_io (q, &completion, 1, 1, NULL);
  if (r != 1) {
    nbdkit_error ("blkioq_do_io: %s", blkio_get_error_msg ());
    return -1;
  }
  if (completion.ret != 0) {
    nbdkit_error ("blkioq_do_io: unexpected read completion.ret %d != 0",
                  completion.ret);
    return -1;
  }
  if (h->mem_region.addr)
    memcpy (buf, h->mem_region.addr, count);

  return 0;
}

/* Write data to the device. */
static int
bio_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
            uint32_t flags)
{
  const bool fua = flags & NBDKIT_FLAG_FUA;
  struct handle *h = handle;
  int r;
  struct blkioq *q = blkio_get_queue (h->b, 0);
  struct blkio_completion completion;
  uint32_t bio_flags;

  if (h->mem_region.addr && count > MAX_BOUNCE_BUFFER) {
    nbdkit_error ("request too large for bounce buffer");
    return -1;
  }

  bio_flags = 0;
  if (fua) bio_flags |= BLKIO_REQ_FUA;
  if (h->mem_region.addr)
    memcpy (h->mem_region.addr, buf, count);
  blkioq_write (q, offset, h->mem_region.addr ? : buf, count, NULL, bio_flags);
  r = blkioq_do_io (q, &completion, 1, 1, NULL);
  if (r != 1) {
    nbdkit_error ("blkioq_do_io: %s", blkio_get_error_msg ());
    return -1;
  }
  if (completion.ret != 0) {
    nbdkit_error ("blkioq_do_io: unexpected write completion.ret %d != 0",
                  completion.ret);
    return -1;
  }

  return 0;
}

/* Flush. */
static int
bio_flush (void *handle, uint32_t flags)
{
  struct handle *h = handle;
  int r;
  struct blkioq *q = blkio_get_queue (h->b, 0);
  struct blkio_completion completion;

  blkioq_flush (q, NULL, 0);
  r = blkioq_do_io (q, &completion, 1, 1, NULL);
  if (r != 1) {
    nbdkit_error ("blkioq_do_io: %s", blkio_get_error_msg ());
    return -1;
  }
  if (completion.ret != 0) {
    nbdkit_error ("blkioq_do_io: unexpected flush completion.ret %d != 0",
                  completion.ret);
    return -1;
  }

  return 0;
}

/* Write zeroes. */
static int
bio_zero (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  const bool fua = flags & NBDKIT_FLAG_FUA;
  const bool may_trim = flags & NBDKIT_FLAG_MAY_TRIM;
  struct handle *h = handle;
  int r;
  struct blkioq *q = blkio_get_queue (h->b, 0);
  struct blkio_completion completion;
  uint32_t bio_flags;

  bio_flags = 0;
  if (fua) bio_flags |= BLKIO_REQ_FUA;
  if (!may_trim) bio_flags |= BLKIO_REQ_NO_UNMAP;
  /* XXX Could support forcing fast zeroes too. */
  blkioq_write_zeroes (q, offset, count, NULL, bio_flags);
  r = blkioq_do_io (q, &completion, 1, 1, NULL);
  if (r != 1) {
    nbdkit_error ("blkioq_do_io: %s", blkio_get_error_msg ());
    return -1;
  }
  if (completion.ret != 0) {
    nbdkit_error ("blkioq_do_io: "
                  "unexpected write zeroes completion.ret %d != 0",
                  completion.ret);
    return -1;
  }

  return 0;
}

/* Discard. */
static int
bio_trim (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  const bool fua = flags & NBDKIT_FLAG_FUA;
  struct handle *h = handle;
  int r;
  struct blkioq *q = blkio_get_queue (h->b, 0);
  struct blkio_completion completion;
  uint32_t bio_flags;

  bio_flags = 0;
  if (fua) bio_flags |= BLKIO_REQ_FUA;
  blkioq_discard (q, offset, count, NULL, bio_flags);
  r = blkioq_do_io (q, &completion, 1, 1, NULL);
  if (r != 1) {
    nbdkit_error ("blkioq_do_io: %s", blkio_get_error_msg ());
    return -1;
  }
  if (completion.ret != 0) {
    nbdkit_error ("blkioq_do_io: unexpected discard completion.ret %d != 0",
                  completion.ret);
    return -1;
  }

  return 0;
}

static struct nbdkit_plugin plugin = {
  .name               = "blkio",
  .version            = PACKAGE_VERSION,
  .unload             = bio_unload,
  .config             = bio_config,
  .config_complete    = bio_config_complete,
  .config_help        = bio_config_help,
  .magic_config_key   = "driver",
  .open               = bio_open,
  .close              = bio_close,
  .get_size           = bio_get_size,
  .block_size         = bio_block_size,
  .can_write          = bio_can_write,
  .can_flush          = bio_can_write,
  .can_trim           = bio_can_write,
  .can_zero           = bio_can_write,
  .can_fua            = bio_can_fua,
  .pread              = bio_pread,
  .pwrite             = bio_pwrite,
  .flush              = bio_flush,
  .zero               = bio_zero,
  .trim               = bio_trim,
  .errno_is_preserved = 0,
};

NBDKIT_REGISTER_PLUGIN (plugin)
