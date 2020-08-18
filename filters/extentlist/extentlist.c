/* nbdkit
 * Copyright (C) 2019-2020 Red Hat Inc.
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
#include <errno.h>
#include <assert.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "getline.h"
#include "minmax.h"
#include "vector.h"

#define HOLE (NBDKIT_EXTENT_HOLE|NBDKIT_EXTENT_ZERO)

static const char *extentlist;

/* List of extents.  Once we've finally parsed them this will be
 * ordered, non-overlapping and have no gaps.
 */
struct extent {
  uint64_t offset, length;
  uint32_t type;
};
DEFINE_VECTOR_TYPE(extent_list, struct extent);
static extent_list extents;

static void
extentlist_unload (void)
{
  free (extents.ptr);
}

/* Called for each key=value passed on the command line. */
static int
extentlist_config (nbdkit_next_config *next, void *nxdata,
                   const char *key, const char *value)
{
  if (strcmp (key, "extentlist") == 0) {
    if (extentlist != NULL) {
      nbdkit_error ("extentlist cannot appear twice");
      exit (EXIT_FAILURE);
    }
    extentlist = value;
    return 0;
  }
  else
    return next (nxdata, key, value);
}

static int
extentlist_config_complete (nbdkit_next_config_complete *next, void *nxdata)
{
  if (extentlist == NULL) {
    nbdkit_error ("you must supply the extentlist parameter "
                  "on the command line");
    return -1;
  }

  return next (nxdata);
}

static int
compare_offsets (const struct extent *e1, const struct extent *e2)
{
  if (e1->offset < e2->offset)
    return -1;
  else if (e1->offset > e2->offset)
    return 1;
  else
    return 0;
}

static int
compare_ranges (const void *ev1, const struct extent *e2)
{
  const struct extent *e1 = ev1;

  if (e1->offset < e2->offset)
    return -1;
  else if (e1->offset >= e2->offset + e2->length)
    return 1;
  else
    return 0;
}

/* Similar to parse_extents in plugins/sh/methods.c */
static void
parse_extentlist (void)
{
  FILE *fp;
  CLEANUP_FREE char *line = NULL;
  size_t linelen = 0;
  ssize_t len;
  size_t i;
  uint64_t end;

  assert (extentlist != NULL);
  assert (extents.ptr == NULL);
  assert (extents.size == 0);

  fp = fopen (extentlist, "r");
  if (!fp) {
    nbdkit_error ("open: %s: %m", extentlist);
    exit (EXIT_FAILURE);
  }

  while ((len = getline (&line, &linelen, fp)) != -1) {
    const char *delim = " \t";
    char *sp, *p;
    int64_t offset, length;
    uint32_t type;

    if (len > 0 && line[len-1] == '\n') {
      line[len-1] = '\0';
      len--;
    }

    if ((p = strtok_r (line, delim, &sp)) == NULL) {
    parse_error:
      nbdkit_error ("%s: cannot parse %s", extentlist, line);
      exit (EXIT_FAILURE);
    }
    offset = nbdkit_parse_size (p);
    if (offset == -1)
      exit (EXIT_FAILURE);

    if ((p = strtok_r (NULL, delim, &sp)) == NULL)
      goto parse_error;
    length = nbdkit_parse_size (p);
    if (length == -1)
      exit (EXIT_FAILURE);

    /* Skip zero length extents.  Makes the rest of the code easier. */
    if (length == 0)
      continue;

    if ((p = strtok_r (NULL, delim, &sp)) == NULL)
      /* empty type field means allocated data (0) */
      type = 0;
    else if (sscanf (p, "%" SCNu32, &type) == 1)
      ;
    else {
      type = 0;
      if (strstr (p, "hole") != NULL)
        type |= NBDKIT_EXTENT_HOLE;
      if (strstr (p, "zero") != NULL)
        type |= NBDKIT_EXTENT_ZERO;
    }

    if (extent_list_append (&extents,
                            (struct extent){.offset = offset, .length=length,
                                            .type=type}) == -1) {
      nbdkit_error ("realloc: %m");
      exit (EXIT_FAILURE);
    }
  }

  fclose (fp);

  /* Sort the extents by offset. */
  extent_list_sort (&extents, compare_offsets);

  /* There must not be overlaps at this point. */
  end = 0;
  for (i = 0; i < extents.size; ++i) {
    if (extents.ptr[i].offset < end ||
        extents.ptr[i].offset + extents.ptr[i].length < extents.ptr[i].offset) {
      nbdkit_error ("extents in the extent list are overlapping");
      exit (EXIT_FAILURE);
    }
    end = extents.ptr[i].offset + extents.ptr[i].length;
  }

  /* If there's a gap at the beginning, insert a hole|zero extent. */
  if (extents.size == 0 || extents.ptr[0].offset > 0) {
    end = extents.size == 0 ? UINT64_MAX : extents.ptr[0].offset;
    if (extent_list_insert (&extents,
                            (struct extent){.offset = 0, .length = end,
                                            .type = HOLE},
                            0) == -1) {
      nbdkit_error ("realloc: %m");
      exit (EXIT_FAILURE);
    }
  }

  /* Now insert hole|zero extents after every extent where there
   * is a gap between that extent and the next one.
   */
  for (i = 0; i < extents.size-1; ++i) {
    end = extents.ptr[i].offset + extents.ptr[i].length;
    if (end < extents.ptr[i+1].offset)
      if (extent_list_insert (&extents,
                              (struct extent){.offset = end,
                                              .length = extents.ptr[i+1].offset - end,
                                              .type = HOLE},
                              i+1) == -1) {
        nbdkit_error ("realloc: %m");
        exit (EXIT_FAILURE);
      }
  }

  /* If there's a gap at the end, insert a hole|zero extent. */
  end = extents.ptr[extents.size-1].offset + extents.ptr[extents.size-1].length;
  if (end < UINT64_MAX) {
    if (extent_list_append (&extents,
                            (struct extent){.offset = end,
                                            .length = UINT64_MAX-end,
                                            .type = HOLE}) == -1) {
      nbdkit_error ("realloc: %m");
      exit (EXIT_FAILURE);
    }
  }

  /* Debug the final list. */
  for (i = 0; i < extents.size; ++i) {
    nbdkit_debug ("extentlist: "
                  "extent[%zu] = %" PRIu64 "-%" PRIu64 " (length %" PRIu64 ")"
                  " type %" PRIu32,
                  i, extents.ptr[i].offset,
                  extents.ptr[i].offset + extents.ptr[i].length - 1,
                  extents.ptr[i].length,
                  extents.ptr[i].type);
  }
}

static int
extentlist_get_ready (nbdkit_next_get_ready *next, void *nxdata,
                      int thread_model)
{
  parse_extentlist ();

  return next (nxdata);
}

static int
extentlist_can_extents (struct nbdkit_next_ops *next_ops, void *nxdata,
                        void *handle)
{
  return 1;
}

/* Use ‘-D extentlist.lookup=1’ to debug the function below. */
int extentlist_debug_lookup = 0;

/* Read extents. */
static int
extentlist_extents (struct nbdkit_next_ops *next_ops, void *nxdata,
                    void *handle, uint32_t count, uint64_t offset,
                    uint32_t flags,
                    struct nbdkit_extents *ret_extents,
                    int *err)
{
  const struct extent eoffset = { .offset = offset };
  struct extent *p;
  ssize_t i;
  uint64_t end;

  /* Find the starting point in the extents list. */
  p = extent_list_search (&extents, &eoffset, compare_ranges);
  assert (p != NULL);
  i = p - extents.ptr;

  /* Add extents to the output. */
  while (count > 0) {
    if (extentlist_debug_lookup)
      nbdkit_debug ("extentlist lookup: "
                    "loop i=%zd count=%" PRIu32 " offset=%" PRIu64,
                    i, count, offset);

    end = extents.ptr[i].offset + extents.ptr[i].length;
    if (nbdkit_add_extent (ret_extents, offset, end - offset,
                           extents.ptr[i].type) == -1)
      return -1;

    count -= MIN (count, end-offset);
    offset = end;
    i++;
  }

  return 0;
}

static struct nbdkit_filter filter = {
  .name              = "extentlist",
  .longname          = "nbdkit extentlist filter",
  .unload            = extentlist_unload,
  .config            = extentlist_config,
  .config_complete   = extentlist_config_complete,
  .get_ready         = extentlist_get_ready,
  .can_extents       = extentlist_can_extents,
  .extents           = extentlist_extents,
};

NBDKIT_REGISTER_FILTER(filter)
