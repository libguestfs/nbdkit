/* nbdkit
 * Copyright (C) 2013-2020 Red Hat Inc.
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

/* This defines the generic allocator interface used by
 * nbdkit-memory-plugin (and elsewhere) allocator=<type>.  It is
 * implemented by specific allocators such as sparse.c.
 *
 * All allocators have an implicit size and grow when required.
 *
 * All allocators do their own locking if required.
 */

#ifndef NBDKIT_ALLOCATOR_H
#define NBDKIT_ALLOCATOR_H

#include <stdbool.h>
#include <stdint.h>

struct nbdkit_extents;

struct allocator {
  /* Allocator type (eg. "sparse").
   * This does not include the parameters.
   */
  const char *type;

  /* Debug flag. */
  bool debug;

  /* Free the allocator instance. */
  void (*free) (struct allocator *a);

  /* Set the size hint.  The size hint is used in various ways by some
   * allocators, and ignored by others.
   *
   * The sparse and zstd array allocators ignore the size hint.
   *
   * The malloc allocator uses it to reserve the expected size of disk
   * in memory (especially important if using mlock so we fail during
   * start-up if there's not enough RAM).
   *
   * Note this does not set or enforce the virtual size of the disk,
   * nor does it implement bounds checking.
   */
  int (*set_size_hint) (struct allocator *a, uint64_t size)
  __attribute__((__nonnull__ (1)));

  /* Read bytes from [offset, offset+count-1] and copy into buf.
   */
  int (*read) (struct allocator *a, void *buf,
               uint32_t count, uint64_t offset)
  __attribute__((__nonnull__ (1, 2)));

  /* Write bytes from buf to [offset, offset+count-1].  Because this
   * can allocate memory, it can fail (returning -1).
   */
  int (*write) (struct allocator *a, const void *buf,
                uint32_t count, uint64_t offset)
  __attribute__((__nonnull__ (1, 2)));

  /* Fill range [offset, offset+count-1] with a single byte ‘c’.
   * If c == '\0', this is the same as .zero below.
   */
  int (*fill) (struct allocator *a, char c, uint32_t count, uint64_t offset)
  __attribute__((__nonnull__ (1)));

  /* Zero range [offset, offset+count-1].  For all allocators zero and
   * trim are the same operation.
   */
  int (*zero) (struct allocator *a, uint32_t count, uint64_t offset)
  __attribute__((__nonnull__ (1)));

  /* Blit (copy) between two allocators.  Copy count bytes from
   * a1.[offset1, offset1+count-1] to a2.[offset2, offset2+count-1].
   *
   * Note you have to call the destination blit function, ie:
   * a2->blit (a1, a2, ...)
   *
   * It's permitted for the allocators to have different types.
   * However you cannot use this to copy within a single allocator
   * (because of locks), ie. a1 must != a2.
   */
  int (*blit) (struct allocator *a1, struct allocator *a2,
               uint32_t count, uint64_t offset1, uint64_t offset2)
  __attribute__((__nonnull__ (1, 2)));

  /* Return information about allocated pages and holes. */
  int (*extents) (struct allocator *a,
                  uint32_t count, uint64_t offset,
                  struct nbdkit_extents *extents)
  __attribute__((__nonnull__ (1, 4)));
};

/* Create a new allocator, usually from the type passed in the
 * allocator=<type> parameter on the nbdkit command line (but you can
 * also create your own internal allocators this way).
 *
 * The debug parameter can be attached to a plugin-specific -D option
 * to provide extra debugging.
 *
 * Note that the type pointer is copied to the first field of the
 * returned struct allocator, so it must be statically allocated (or
 * at least live as long as the allocator).
 *
 * On error, calls nbdkit_error and returns NULL.
 */
extern struct allocator *create_allocator (const char *type, bool debug)
  __attribute__((__nonnull__ (1)));

#define CLEANUP_FREE_ALLOCATOR \
  __attribute__((cleanup (cleanup_free_allocator)))
extern void cleanup_free_allocator (struct allocator **ap);

#endif /* NBDKIT_ALLOCATOR_H */
