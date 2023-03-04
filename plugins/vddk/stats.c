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
#include <stdint.h>
#include <inttypes.h>

#include <pthread.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "vector.h"

#include "vddk.h"

/* Debug flags. */
NBDKIT_DLL_PUBLIC int vddk_debug_stats;

pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

/* For each VDDK API define a variable to store the time taken (used
 * to implement -D vddk.stats=1).
 */
#define STUB(fn, ret, args) struct vddk_stat stats_##fn = { .name = #fn }
#define OPTIONAL_STUB(fn, ret, args) STUB (fn, ret, args)
#include "vddk-stubs.h"
#undef STUB
#undef OPTIONAL_STUB

DEFINE_VECTOR_TYPE (statlist, struct vddk_stat);

static int
stat_compare (const void *vp1, const void *vp2)
{
  const struct vddk_stat *st1 = vp1;
  const struct vddk_stat *st2 = vp2;

  /* Note: sorts in reverse order of time spent in each API call. */
  if (st1->usecs < st2->usecs) return 1;
  else if (st1->usecs > st2->usecs) return -1;
  else return 0;
}

static const char *
api_name_without_prefix (const char *name)
{
  return strncmp (name, "VixDiskLib_", 11) == 0 ? name + 11 : name;
}

void
display_stats (void)
{
  statlist stats = empty_vector;
  size_t i;

  if (!vddk_debug_stats) return;

#define STUB(fn, ret, args) statlist_append (&stats, stats_##fn)
#define OPTIONAL_STUB(fn, ret, args) STUB (fn, ret, args)
#include "vddk-stubs.h"
#undef STUB
#undef OPTIONAL_STUB

  qsort (stats.ptr, stats.len, sizeof stats.ptr[0], stat_compare);

  nbdkit_debug ("VDDK function stats (-D vddk.stats=1):");
  nbdkit_debug ("%-24s  %15s %5s %15s",
                "VixDiskLib_...", "µs", "calls", "bytes");
  for (i = 0; i < stats.len; ++i) {
    if (stats.ptr[i].usecs) {
      if (stats.ptr[i].bytes > 0)
        nbdkit_debug ("  %-22s %15" PRIi64 " %5" PRIu64 " %15" PRIu64,
                      api_name_without_prefix (stats.ptr[i].name),
                      stats.ptr[i].usecs,
                      stats.ptr[i].calls,
                      stats.ptr[i].bytes);
      else
        nbdkit_debug ("  %-22s %15" PRIi64 " %5" PRIu64,
                      api_name_without_prefix (stats.ptr[i].name),
                      stats.ptr[i].usecs,
                      stats.ptr[i].calls);
    }
  }
  statlist_reset (&stats);
}
