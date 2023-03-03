/* nbdkit
 * Copyright (C) 2018-2020 François Revol.
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
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include <pthread.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "getline.h"
#include "vector.h"

struct range {
  int64_t start;
  int64_t end;
  int64_t size;
  char status;
};
DEFINE_VECTOR_TYPE (ranges, struct range);

struct mapfile {
  int ranges_count;
  ranges ranges;
};

static struct mapfile map = { 0, empty_vector };

static int
parse_mapfile (const char *filename)
{
  FILE *fp = NULL;
  CLEANUP_FREE char *line = NULL;
  size_t linelen = 0;
  ssize_t len;
  int ret = -1;
  int status_seen = 0;

  fp = fopen (filename, "r");
  if (!fp) {
    nbdkit_error ("%s: ddrescue: fopen: %m", filename);
    goto out;
  }

  while ((len = getline (&line, &linelen, fp)) != -1) {
    int64_t offset, length;
    char status;

    if (len > 0 && line[len-1] == '\n') {
      line[len-1] = '\0';
      len--;
    }

    if (len > 0 && line[0] == '#')
      continue;

    if (len > 0 && !status_seen) {
      /* status line, ignore it for now */
      status_seen = 1;
      nbdkit_debug ("%s: skipping status line: '%s'", filename, line);
      continue;
    }

    if (sscanf (line, "%" SCNi64 "\t%" SCNi64 "\t%c",
                &offset, &length, &status) == 3) {
      if (offset < 0) {
        nbdkit_error ("block offset must not be negative");
        return -1;
      }
      if (length < 0) {
        nbdkit_error ("block length must not be negative");
        return -1;
      }
      if (status == '+') {
        struct range new_range = { .start = offset, .end = offset + length - 1,
                                   .size = length, .status = status };

        if (ranges_append (&map.ranges, new_range) == -1) {
          nbdkit_error ("%s: ddrescue: realloc: %m", filename);
          goto out;
        }
      }

      nbdkit_debug ("%s: range: 0x%" PRIx64 " 0x%" PRIx64 " '%c'",
                    filename, offset, length, status);
    }
  }

  ret = 0;

 out:
  if (fp)
    fclose (fp);
  return ret;
}

/* On unload, free the mapfile data. */
static void
ddrescue_unload (void)
{
  free (map.ranges.ptr);
}

static int
ddrescue_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
                 const char *key, const char *value)
{
  if (strcmp (key, "ddrescue-mapfile") == 0) {
    if (parse_mapfile (value) == -1)
      return -1;
    return 0;
  }

  else
    return next (nxdata, key, value);
}

#define ddrescue_config_help \
  "ddrescue-mapfile=...     Specify ddrescue mapfile to use"

/* We need this because otherwise the layer below can_write is called
 * and that might return true (eg. if the plugin has a pwrite method
 * at all), resulting in writes being passed through to the layer
 * below.
 */
static int
ddrescue_can_write (nbdkit_next *next,
                    void *handle)
{
  return 0;
}

static int
ddrescue_can_cache (nbdkit_next *next,
                    void *handle)
{
  return 0;
}

/* Read data. */
static int
ddrescue_pread (nbdkit_next *next,
                void *handle, void *buf, uint32_t count, uint64_t offset,
                uint32_t flags, int *err)
{
  size_t i;

  for (i = 0; i < map.ranges.len; i++) {
    if (map.ranges.ptr[i].status != '+')
      continue;
    if (offset >= map.ranges.ptr[i].start && offset <= map.ranges.ptr[i].end) {
      if (offset + count - 1 <= map.ranges.ptr[i].end) {
        /* entirely contained within this range */
        return next->pread (next, buf, count, offset, flags, err);
      }
    }
  }
  /* read was not fully covered */
  nbdkit_debug ("ddrescue: pread: range: 0x%" PRIx64 " 0x%" PRIx32
                " failing with EIO", offset, count);
  *err = EIO;
  return -1;
}

static struct nbdkit_filter filter = {
  .name              = "ddrescue",
  .longname          = "nbdkit ddrescue mapfile filter",
  .unload            = ddrescue_unload,
  .config            = ddrescue_config,
  .config_help       = ddrescue_config_help,
  .can_write         = ddrescue_can_write,
  .can_cache         = ddrescue_can_cache,
  .pread             = ddrescue_pread,
};

NBDKIT_REGISTER_FILTER (filter)
