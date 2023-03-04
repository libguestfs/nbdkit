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

/* This filter is implemented using a Token Bucket
 * (https://en.wikipedia.org/wiki/Token_bucket).  There are two
 * buckets per connection (one each for reading and writing) and two
 * global buckets (also for reading and writing).
 *
 *      │       │ ← bucket->capacity
 *      │       │
 *      │░░░░░░░│ ← bucket->level
 *      │░░░░░░░│
 *      │░░░░░░░│
 *      └───────┘
 *
 * We add tokens at the desired rate (the per-connection rate for the
 * connection buckets, and the global rate for the global buckets).
 * Note that we don't actually keep the buckets updated in real time
 * because as a filter we are called asynchronously.  Instead for each
 * bucket we store the last time we were called and add the
 * appropriate number of tokens when we are called next.
 *
 * The bucket capacity controls the burstiness allowed.  This is
 * hard-coded at the moment but could be configurable.  All buckets
 * start off full.
 *
 * When a packet is to be read or written, if there are sufficient
 * tokens in the bucket then the packet may be immediately passed
 * through to the underlying plugin.  The number of bits used is
 * deducted from the appropriate per-connection and global bucket.
 *
 * If there are insufficient tokens then the packet must be delayed.
 * This is done by inserting a sleep which has an estimated length
 * that is long enough based on the rate at which enough tokens will
 * replenish the bucket to allow the packet to be sent next time.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include <nbdkit-filter.h>

#include "minmax.h"
#include "tvdiff.h"

#include "bucket.h"

NBDKIT_DLL_PUBLIC int rate_debug_bucket;          /* -D rate.bucket=1 */

void
bucket_init (struct bucket *bucket, uint64_t rate, double capacity_secs)
{
  bucket->rate = rate;

  /* Store the capacity passed to this function.  We will need this if
   * we adjust the rate dynamically.
   */
  bucket->capacity_secs = capacity_secs;

  /* Capacity is expressed in seconds, but we want to know the
   * capacity in tokens, so multiply by the rate to get this.
   */
  bucket->capacity = rate * capacity_secs;

  /* Buckets start off full. */
  bucket->level = bucket->capacity;

  gettimeofday (&bucket->tv, NULL);
}

uint64_t
bucket_adjust_rate (struct bucket *bucket, uint64_t rate)
{
  uint64_t old_rate = bucket->rate;

  bucket->rate = rate;
  bucket->capacity = rate * bucket->capacity_secs;
  if (bucket->level > bucket->capacity)
    bucket->level = bucket->capacity;
  return old_rate;
}

uint64_t
bucket_run (struct bucket *bucket, const char *bucket_name,
            uint64_t n, struct timespec *ts)
{
  struct timeval now;
  int64_t usec;
  uint64_t add, nsec;

  /* rate == 0 is a special case meaning that there is no limit being
   * enforced.
   */
  if (bucket->rate == 0)
    return 0;

  gettimeofday (&now, NULL);

  /* Work out how much time has elapsed since we last added tokens to
   * the bucket, and add the correct number of tokens.
   */
  usec = tvdiff_usec (&bucket->tv, &now);
  if (usec < 0)      /* Maybe happens if system time not monotonic? */
    usec = 0;

  add = bucket->rate * usec / 1000000;
  add = MIN (add, bucket->capacity - bucket->level);
  if (rate_debug_bucket)
    nbdkit_debug ("bucket %s: adding %" PRIu64 " tokens, new level %" PRIu64,
                  bucket_name, add, bucket->level + add);
  bucket->level += add;
  bucket->tv = now;

  /* Can we deduct N tokens from the bucket?  If yes then we're good,
   * and we can return 0 which means the caller won't sleep.
   */
  if (bucket->level >= n) {
    if (rate_debug_bucket)
      nbdkit_debug ("bucket %s: deducting %" PRIu64 " tokens", bucket_name, n);
    bucket->level -= n;
    return 0;
  }

  if (rate_debug_bucket)
    nbdkit_debug ("bucket %s: deducting %" PRIu64 " tokens, bucket empty, "
                  "need another %" PRIu64 " tokens",
                  bucket_name, bucket->level, n - bucket->level);

  n -= bucket->level;
  bucket->level = 0;

  /* Now we need to estimate how long it will take to add N tokens to
   * the bucket, which is how long the caller must sleep for.
   */
  nsec = 1000000000 * n / bucket->rate;
  ts->tv_sec = nsec / 1000000000;
  ts->tv_nsec = nsec % 1000000000;

  if (rate_debug_bucket)
    nbdkit_debug ("bucket %p: sleeping for %.1f seconds", bucket,
                  nsec / 1000000000.);

  return n;
}
