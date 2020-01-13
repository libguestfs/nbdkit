/* nbdkit
 * Copyright (C) 2018-2019 Red Hat Inc.
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

struct range {
  int64_t start;
  int64_t end;
  int64_t size;
  char status;
};

struct mapfile {
  int ranges_count;
  struct range *ranges;
};

static struct mapfile map = { 0, NULL };

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
    const char *delim = " \t";
    char *sp, *p;
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

    if (sscanf (line, "%" SCNi64 "\t%" SCNi64 "\t%c", &offset, &length, &status) == 3) {
      if (offset < 0) {
        nbdkit_error ("block offset must not be negative");
        return -1;
      }
      if (length < 0) {
        nbdkit_error ("block length must not be negative");
        return -1;
      }
      if (status == '+') {
        int i = map.ranges_count++;
        map.ranges = realloc(map.ranges, map.ranges_count * sizeof(struct range));
        if (map.ranges == NULL) {
          nbdkit_error ("%s: ddrescue: realloc: %m", filename);
          goto out;
        }
        map.ranges[i].start = offset;
        map.ranges[i].end = offset + length - 1;
        map.ranges[i].size = length;
        map.ranges[i].status = status;
      }
      
      nbdkit_debug ("%s: range: 0x%" PRIx64 " 0x%" PRIx64 " '%c'", filename, offset, length, status);
    }
  }

  ret = 0;

 out:
  if (fp)
    fclose (fp);
  return ret;
}


static void
ddrescue_load (void)
{
}

/* On unload, free the sparse array. */
static void
ddrescue_unload (void)
{
  free (map.ranges);
  map.ranges = NULL;
  map.ranges_count = 0;
}

static int
ddrescue_config (nbdkit_next_config *next, void *nxdata,
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
 * below.  This is possibly a bug in nbdkit.
 */
static int
ddrescue_can_write (struct nbdkit_next_ops *next_ops, void *nxdata,
                    void *handle)
{
  return 0;
}

static int
ddrescue_can_cache (struct nbdkit_next_ops *next_ops, void *nxdata,
                    void *handle)
{
  return 0;
}

/* Read data. */
static int
ddrescue_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
                void *handle, void *buf, uint32_t count, uint64_t offset,
                uint32_t flags, int *err)
{
  int i;

  for (i = 0; i < map.ranges_count; i++) {
    if (map.ranges[i].status != '+')
      continue;
    if (offset >= map.ranges[i].start && offset <= map.ranges[i].end) {
      if (offset + count - 1 <= map.ranges[i].end) {
        /* entirely contained within this range */
        return next_ops->pread (nxdata, buf, count, offset, flags, err);
      }
    }
  }
  /* read was not fully covered */
  *err = EIO;
  return -1;
}

static struct nbdkit_filter filter = {
  .name              = "ddrescue",
  .longname          = "nbdkit ddrescue mapfile filter",
  .load              = ddrescue_load,
  .unload            = ddrescue_unload,
  .config            = ddrescue_config,
  .config_help       = ddrescue_config_help,
  .can_write         = ddrescue_can_write,
  .can_cache         = ddrescue_can_cache,
  .pread             = ddrescue_pread,
};

NBDKIT_REGISTER_FILTER(filter)
