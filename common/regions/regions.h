/* nbdkit
 * Copyright (C) 2018 Red Hat Inc.
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

#ifndef NBDKIT_REGIONS_H
#define NBDKIT_REGIONS_H

#include <stdint.h>
#include <assert.h>

/* This defines a very simple structure used to define the virtual
 * disk in the partitioning and floppy plugins.
 *
 * We split the virtual disk into non-overlapping, contiguous regions.
 * These are stored in an array, ordered by address.
 *
 * Each region can be one of several types, referring to a backing
 * file, some data stored in memory, or zero padding.
 */

/* Region type. */
enum region_type {
  region_file,        /* contents of the i'th file */
  region_data,        /* pointer to in-memory data */
  region_zero,        /* padding */
};

/* Region. */
struct region {
  uint64_t start, len, end;    /* byte offsets; end = start + len - 1 */
  enum region_type type;
  union {
    size_t i;                  /* region_file: i'th file */
    const unsigned char *data; /* region_data: data */
  } u;

  /* Optional name or description of this region.  This is not used by
   * the regions code but can be added to regions to make debugging
   * easier.
   */
  const char *description;
};

/* Array of regions. */
struct regions {
  struct region *regions;
  size_t nr_regions;
};

extern void init_regions (struct regions *regions)
  __attribute__((__nonnull__ (1)));
extern void free_regions (struct regions *regions)
  __attribute__((__nonnull__ (1)));

/* Look up the region corresponding to the given offset.  If the
 * offset is inside the disk image then this cannot return NULL.
 */
extern const struct region *find_region (const struct regions *regions,
                                         uint64_t offset)
  __attribute__((__nonnull__ (1)));

/* This is the low level function for constructing the list of
 * regions.  It appends one region to the list, checking that the
 * invariants described above (about the regions being non-overlapping
 * and contiguous) is maintained.  Note it is not possible to
 * construct regions out of order using this function.
 */
extern int append_one_region (struct regions *regions, struct region region)
  __attribute__((__nonnull__ (1)));

/* Used when iterating over the list of regions. */
static inline const struct region * __attribute__((__nonnull__ (1)))
get_region (const struct regions *regions, size_t i)
{
  assert (i < regions->nr_regions);
  return &regions->regions[i];
}

/* Return the number of regions. */
static inline size_t __attribute__((__nonnull__ (1)))
nr_regions (struct regions *regions)
{
  return regions->nr_regions;
}

/* Return the virtual size of the disk. */
static inline int64_t __attribute__((__nonnull__ (1)))
virtual_size (struct regions *regions)
{
  if (regions->nr_regions == 0)
    return 0;
  else
    return regions->regions[regions->nr_regions-1].end + 1;
}

#endif /* NBDKIT_REGIONS_H */
