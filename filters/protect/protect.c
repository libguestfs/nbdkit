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
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "iszero.h"
#include "minmax.h"
#include "regions.h"
#include "strndup.h"
#include "vector.h"

/* Range definition.
 *
 * A single struct range stores a range from start-end (inclusive).
 * end can be INT64_MAX to indicate the end of the file.
 *
 * 'range_list' stores the list of protected ranges, unsorted.
 */
struct range { uint64_t start, end; const char *description; };
DEFINE_VECTOR_TYPE (ranges, struct range);
static ranges range_list;

/* region_list covers the whole address space with protected and
 * unprotected ranges.
 */
static regions region_list;

static void
protect_unload (void)
{
  free (region_list.ptr);
  free (range_list.ptr);
}

/* Parse "START-END" into a range or "~START-END" into two ranges.
 * Adds the range(s) to range_list, or exits with an error.
 */
static void
parse_range (const char *value)
{
  CLEANUP_FREE char *start_str = NULL;
  const char *end_str;
  uint64_t start, end;
  bool negate = false;
  const char *description = value;

  if (value[0] == '~') {
    negate = true;
    value++;
  }

  end_str = strchr (value, '-');
  if (end_str == NULL) {
    nbdkit_error ("cannot parse range, missing '-': %s", description);
    exit (EXIT_FAILURE);
  }
  start_str = strndup (value, end_str - value);
  if (start_str == NULL) {
    nbdkit_error ("strndup: %m");
    exit (EXIT_FAILURE);
  }
  end_str++;

  if (start_str[0] == '\0')
    start = 0;
  else if (nbdkit_parse_uint64_t ("range", start_str, &start) == -1)
    exit (EXIT_FAILURE);

  if (end_str[0] == '\0')
    end = INT64_MAX;
  else if (nbdkit_parse_uint64_t ("range", end_str, &end) == -1)
    exit (EXIT_FAILURE);

  if (end < start) {
    nbdkit_error ("invalid range, end < start: %s", description);
    exit (EXIT_FAILURE);
  }

  if (!negate) {
    struct range range =
      { .start = start, .end = end, .description = description };

    if (ranges_append (&range_list, range) == -1) {
      nbdkit_error ("ranges_append: %m");
      exit (EXIT_FAILURE);
    }
  }
  else {                        /* ~START-END creates up to two ranges */
    struct range range = { .description = description };

    if (start > 0) {
      range.start = 0;
      range.end = start - 1;
      if (ranges_append (&range_list, range) == -1) {
        nbdkit_error ("ranges_append: %m");
        exit (EXIT_FAILURE);
      }
    }
    if (end < INT64_MAX) {
      range.start = end + 1;
      range.end = INT64_MAX;
      if (ranges_append (&range_list, range) == -1) {
        nbdkit_error ("ranges_append: %m");
        exit (EXIT_FAILURE);
      }
    }
  }
}

static int
protect_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
                const char *key, const char *value)
{
  if (strcmp (key, "protect") == 0) {
    parse_range (value);
    return 0;
  }
  else
    return next (nxdata, key, value);
}

static int
compare_ranges (const struct range *r1, const struct range *r2)
{
  if (r1->start < r2->start)
    return -1;
  else if (r1->start > r2->start)
    return 1;
  else
    return 0;
}

static void
append_unprotected_region (uint64_t end)
{
  if (append_region_end (&region_list, "unprotected", end,
                         0, 0, region_data, NULL) == -1) {
    nbdkit_error ("append region: %m");
    exit (EXIT_FAILURE);
  }
}

static void
append_protected_region (struct range range)
{
  assert (virtual_size (&region_list) == range.start);

  if (append_region_end (&region_list, range.description, range.end,
                         0, 0, region_data, "protected") == -1) {
    nbdkit_error ("append region: %m");
    exit (EXIT_FAILURE);
  }
}

static int
protect_config_complete (nbdkit_next_config_complete *next,
                         nbdkit_backend *nxdata)
{
  size_t i;

  if (range_list.len > 0) {
    /* Order the ranges and combine adjacent and overlapping ranges. */
    ranges_sort (&range_list, compare_ranges);

    /* Combine adjacent and overlapping ranges. */
    for (i = 0; i < range_list.len - 1; ++i) {
      /* This is true because we've sorted the ranges. */
      assert (range_list.ptr[i].start <= range_list.ptr[i+1].start);

      /* Adjacent or overlapping with the next range? */
      if (range_list.ptr[i].end + 1 >= range_list.ptr[i+1].start) {
        range_list.ptr[i].end = range_list.ptr[i+1].end;
        ranges_remove (&range_list, i+1);
        i--;
        continue;
      }
    }
  }

  /* Now convert these to a complete list of regions covering the
   * whole 64 bit address space.
   *
   * Insert an initial unprotected region before the first protected
   * range.
   */
  if (range_list.len != 0 && range_list.ptr[0].start > 0)
    append_unprotected_region (range_list.ptr[0].start - 1);

  for (i = 0; i < range_list.len; ++i) {
    append_protected_region (range_list.ptr[i]);

    /* Insert an unprotected region before the next range. */
    if (i+1 < range_list.len)
      append_unprotected_region (range_list.ptr[i+1].start - 1);
  }

  /* Insert a final unprotected region at the end. */
  if ((uint64_t) virtual_size (&region_list) < (uint64_t) INT64_MAX)
    append_unprotected_region (INT64_MAX);

  return next (nxdata);
}

#define protect_config_help \
  "protect=<START>-<END>      Protect range of bytes START-END (inclusive)."

/* -D protect.write=1 to debug write checks. */
NBDKIT_DLL_PUBLIC int protect_debug_write = 0;

/* Check the proposed write operation.
 *
 * If offset and count contain any protected ranges, then we check
 * that the write does not modify those ranges.  If buf != NULL then
 * we check that the data proposed to be written to the protected
 * ranges matches what we read from the plugin.  If buf == NULL then
 * we check that the plugin reads zero for the protected ranges.
 */
static int
check_write (nbdkit_next *next,
             uint32_t count, uint64_t offset, const void *buf, int *err)
{
  while (count > 0) {
    const struct region *region;
    bool protected;
    uint64_t len;
    int r;

    region = find_region (&region_list, offset);
    assert (region != NULL);
    assert (region->type == region_data);

    protected = region->u.data != NULL;
    len = MIN (region->end - offset + 1, (uint64_t) count);
    assert (len > 0);

    if (protect_debug_write)
      nbdkit_debug ("protect: %s offset %" PRIu64 " length %" PRIu64,
                    protected ? "checking protected region"
                    : "skipping unprotected region",
                    offset, len);

    if (protected) {
      bool matches;
      CLEANUP_FREE char *expected = malloc (len);
      if (expected == NULL) {
        nbdkit_error ("malloc: %m");
        *err = errno;
        return -1;
      }

      /* Read the underlying plugin. */
      r = next->pread (next, expected, len, offset, 0, err);
      if (r == -1)
        return -1;              /* read error */

      /* Expected data should match buffer (or zero if buf == NULL). */
      if (buf)
        matches = memcmp (expected, buf, len) == 0;
      else
        matches = is_zero (expected, len);
      if (!matches) {
        nbdkit_error ("protect filter prevented write to protected range %s",
                      region->description);
        *err = EPERM;
        return -1;
      }
    }

    count -= len;
    buf += len;
    offset += len;
  }

  return 0;
}

/* Write data. */
static int
protect_pwrite (nbdkit_next *next,
                void *handle,
                const void *buf,
                uint32_t count, uint64_t offset, uint32_t flags,
                int *err)
{
  if (check_write (next, count, offset, buf, err) == -1)
    return -1;

  return next->pwrite (next, buf, count, offset, flags, err);
}

/* Trim data. */
static int
protect_trim (nbdkit_next *next,
              void *handle, uint32_t count, uint64_t offset, uint32_t flags,
              int *err)
{
  if (check_write (next, count, offset, NULL, err) == -1)
    return -1;

  return next->trim (next, count, offset, flags, err);
}

/* Zero data. */
static int
protect_zero (nbdkit_next *next,
              void *handle, uint32_t count, uint64_t offset, uint32_t flags,
              int *err)
{
  if (check_write (next, count, offset, NULL, err) == -1)
    return -1;

  return next->zero (next, count, offset, flags, err);
}

static struct nbdkit_filter filter = {
  .name              = "protect",
  .longname          = "nbdkit protect filter",
  .unload            = protect_unload,
  .config            = protect_config,
  .config_complete   = protect_config_complete,
  .config_help       = protect_config_help,
  .pwrite            = protect_pwrite,
  .trim              = protect_trim,
  .zero              = protect_zero,
};

NBDKIT_REGISTER_FILTER (filter)
