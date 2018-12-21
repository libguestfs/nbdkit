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

/* Size of a block in the overlay.  A 4K block size means that we need
 * 32 MB of memory to store the bitmap for a 1 TB underlying image.
 */
#define BLKSIZE 4096

/* Initialize the overlay and bitmap. */
extern int blk_init (void);

/* Close the overlay, free the bitmap. */
extern void blk_free (void);

/*----------------------------------------------------------------------
 * ** NOTE **
 *
 * An exclusive lock must be held when you call any function below
 * this line.
 */

/* Allocate or resize the overlay and bitmap. */
extern int blk_set_size (uint64_t new_size);

/* Read a single block from the overlay or plugin. */
extern int blk_read (struct nbdkit_next_ops *next_ops, void *nxdata, uint64_t blknum, uint8_t *block, int *err);

/* Write a single block. */
extern int blk_write (uint64_t blknum, const uint8_t *block, int *err);

/* Flush the overlay to disk. */
extern int blk_flush (void);

#endif /* NBDKIT_BLK_H */
