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

/* These functions are like pread(2)/pwrite(2) but they always read or
 * write the full amount, or fail.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "pread.h"
#include "pwrite.h"

ssize_t
full_pread (int fd, void *buf, size_t count, off_t offset)
{
  ssize_t ret = 0, r;

  while (count > 0) {
    r = pread (fd, buf, count, offset);
    if (r == -1) return -1;
    if (r == 0) {
      /* Presumably the caller wasn't expecting end-of-file here, so
       * return an error.
       */
      errno = EIO;
      return -1;
    }
    ret += r;
    offset += r;
    count -= r;
  }

  return ret;
}

ssize_t
full_pwrite (int fd, const void *buf, size_t count, off_t offset)
{
  ssize_t ret = 0, r;

  while (count > 0) {
    r = pwrite (fd, buf, count, offset);
    if (r == -1) return -1;
    ret += r;
    offset += r;
    count -= r;
  }

  return ret;
}
