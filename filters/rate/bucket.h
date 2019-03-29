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

#ifndef NBDKIT_BUCKET_H
#define NBDKIT_BUCKET_H

#include <stdint.h>
#include <time.h>
#include <sys/time.h>

/* A token bucket. */
struct bucket {
  uint64_t rate;                /* Fill rate.  0 = no limit set. */
  uint64_t capacity;            /* Maximum capacity of the bucket. */
  uint64_t level;               /* How full is the bucket now? */
  struct timeval tv;            /* Last time we updated the level. */
};

/* Initialize the bucket structure.  Capacity is expressed in
 * rate-equivalent seconds.
 */
extern void bucket_init (struct bucket *bucket,
                         uint64_t rate, double capacity);

/* Take up to N tokens from the bucket.  Returns the number
 * of tokens remaining (that could not be taken from the bucket),
 * or 0 if we were able to take all N tokens from the bucket.
 *
 * In the case that the return value > 0, *TS is initialized with the
 * estimated length of time you should sleep.  Note that *TS is _NOT_
 * initialized if the return value == 0, because the caller should not
 * sleep in that case.
 *
 * In the case where the caller needs to sleep, it must make a further
 * call to bucket_run before proceeding, since another thread may have
 * "stolen" the tokens while you were sleeping.
 */
extern uint64_t bucket_run (struct bucket *bucket, uint64_t n,
                            struct timespec *ts);

#endif /* NBDKIT_BUCKET_H */
