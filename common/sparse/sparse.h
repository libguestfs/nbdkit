/* nbdkit
 * Copyright (C) 2017-2018 Red Hat Inc.
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

#ifndef NBDKIT_SPARSE_H
#define NBDKIT_SPARSE_H

#include <config.h>

#include <stdbool.h>

/* This library implements a sparse array of any size up to 2⁶³-1
 * bytes.
 *
 * The array reads as zeroes until something is written.
 *
 * The implementation aims to be reasonably efficient for ordinary
 * sized disks, while permitting huge (but sparse) disks for testing.
 * Everything allocated has to be stored in memory.  There is no
 * temporary file backing.
 *
 * The implementation is not protected by locks and issuing parallel
 * calls will cause corruption.  If your plugin uses this library you
 * will probably need to use a suitable thread model such as
 * SERIALIZE_ALL_REQUESTS.
 */
struct sparse_array;

/* Allocate the empty sparse array. */
struct sparse_array *alloc_sparse_array (bool debug);

/* Free sparse array. */
extern void free_sparse_array (struct sparse_array *sa);

/* Read bytes from the sparse array.
 * Note this can never return an error and never allocates.
 */
extern void sparse_array_read (struct sparse_array *sa, void *buf, uint32_t count, uint64_t offset);

/* Write bytes to the sparse array.
 * This can allocate and can return an error.
 */
extern int sparse_array_write (struct sparse_array *sa, const void *buf, uint32_t count, uint64_t offset);

/* Zero byte range in the sparse array.
 *
 * Zeroing and trimming are the same operation (this implementation
 * does not preallocate, since it's not worthwhile for an in-memory
 * data structure).
 *
 * This may free memory, but never returns an error.
 */
extern void sparse_array_zero (struct sparse_array *sa, uint32_t count, uint64_t offset);

#endif /* NBDKIT_SPARSE_H */
