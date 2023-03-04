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
#include <string.h>
#include <errno.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

/* Create the per-connection handle. */
static void *
zero_open (int readonly)
{
  return NBDKIT_HANDLE_NOT_NEEDED;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Get the disk size. */
static int64_t
zero_get_size (void *handle)
{
  return 0;
}

/* Serves the same data over multiple connections. */
static int
zero_can_multi_conn (void *handle)
{
  return 1;
}

/* Cache. */
static int
zero_can_cache (void *handle)
{
  /* Everything is already in memory, returning this without
   * implementing .cache lets nbdkit do the correct no-op.
   */
  return NBDKIT_CACHE_NATIVE;
}

/* Ideally the read plugin would be optional. */
static int
zero_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
            uint32_t flags)
{
  nbdkit_error ("unexpected call to pread");
  return -1;
}

static struct nbdkit_plugin plugin = {
  .name              = "zero",
  .version           = PACKAGE_VERSION,
  .open              = zero_open,
  .get_size          = zero_get_size,
  .can_multi_conn    = zero_can_multi_conn,
  .can_cache         = zero_can_cache,
  .pread             = zero_pread,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN (plugin)
