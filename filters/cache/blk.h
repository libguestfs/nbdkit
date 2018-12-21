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

#ifndef NBDKIT_BLK_H
#define NBDKIT_BLK_H

/* Initialize the cache and bitmap. */
extern int blk_init (void);

/* Close the cache, free the bitmap. */
extern void blk_free (void);

/*----------------------------------------------------------------------
 * ** NOTE **
 *
 * An exclusive lock must be held when you call any function below
 * this line.
 */

/* Allocate or resize the cache file and bitmap. */
extern int blk_set_size (uint64_t new_size);

/* Read a single block from the cache or plugin. */
extern int blk_read (struct nbdkit_next_ops *next_ops, void *nxdata, uint64_t blknum, uint8_t *block, int *err);

/* Write to the cache and the plugin. */
extern int blk_writethrough (struct nbdkit_next_ops *next_ops, void *nxdata, uint64_t blknum, const uint8_t *block, uint32_t flags, int *err);

/* Write a whole block.
 *
 * If the cache is in writethrough mode, or the FUA flag is set, then
 * this calls blk_writethrough above which will write both to the
 * cache and through to the underlying device.
 *
 * Otherwise it will only write to the cache.
 */
extern int blk_write (struct nbdkit_next_ops *next_ops, void *nxdata, uint64_t blknum, const uint8_t *block, uint32_t flags, int *err);

/* Iterates over each dirty block in the cache. */
typedef int (*block_callback) (uint64_t blknum, void *vp);
extern int for_each_dirty_block (block_callback f, void *vp);

#endif /* NBDKIT_BLK_H */
