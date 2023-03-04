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

#ifndef NBDKIT_REGIONS_H
#define NBDKIT_REGIONS_H

#include <stdint.h>
#include <stdarg.h>
#include <assert.h>

#include "vector.h"

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

/* Vector of struct region. */
DEFINE_VECTOR_TYPE (regions, struct region);

extern void init_regions (regions *regions)
  __attribute__ ((__nonnull__ (1)));
extern void free_regions (regions *regions)
  __attribute__ ((__nonnull__ (1)));

/* Return the number of regions. */
static inline size_t __attribute__ ((__nonnull__ (1)))
nr_regions (regions *rs)
{
  return rs->len;
}

/* Return the virtual size of the disk. */
static inline int64_t __attribute__ ((__nonnull__ (1)))
virtual_size (regions *rs)
{
  if (rs->len == 0)
    return 0;
  else
    return rs->ptr[rs->len-1].end + 1;
}

/* Look up the region corresponding to the given offset.  If the
 * offset is inside the disk image then this cannot return NULL.
 */
extern const struct region *find_region (const regions *regions,
                                         uint64_t offset)
  __attribute__ ((__nonnull__ (1)));

/* Append one region of a given length, plus up to two optional
 * padding regions.
 *
 * pre_aligment (if != 0) describes the required alignment of this
 * region.  A padding region of type region_zero is inserted before
 * the main region if required.
 *
 * post_alignment (if != 0) describes the required alignment after
 * this region.  A padding region of type region_zero is inserted
 * after the main region if required.
 *
 * If type == region_file, it must be followed by u.i parameter.
 * If type == region_data, it must be followed by u.data parameter.
 */
extern int append_region_len (regions *regions,
                              const char *description, uint64_t len,
                              uint64_t pre_aligment, uint64_t post_alignment,
                              enum region_type type, ...);

/* Same as append_region_len (above) but instead of specifying the
 * size of the main region, specify the end byte as an offset.  Note
 * the end byte is included in the region, it's is NOT the end+1 byte.
 */
extern int append_region_end (regions *regions,
                              const char *description, uint64_t end,
                              uint64_t pre_aligment, uint64_t post_alignment,
                              enum region_type type, ...);

#endif /* NBDKIT_REGIONS_H */
