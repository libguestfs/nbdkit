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

/* Replacement for pread for platforms which lack this function. */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>

#ifndef HAVE_PREAD

#include "pread.h"

#ifdef WIN32

/* Replacement pread for Win32. */

#include "nbdkit-plugin.h"
#include <windows.h>

ssize_t
pread (int fd, void *buf, size_t count, off_t offset)
{
  DWORD r;
  OVERLAPPED ovl;
  HANDLE h;

  memset (&ovl, 0, sizeof ovl);
  /* Seriously WTF Windows? */
  ovl.Offset = offset & 0xffffffff;
  ovl.OffsetHigh = offset >> 32;

  h = (HANDLE) _get_osfhandle (fd);
  if (h == INVALID_HANDLE_VALUE) {
    nbdkit_debug ("ReadFile: bad handle");
    errno = EIO;
    return -1;
  }

  /* XXX Will fail weirdly if count is larger than 32 bits. */
  if (!ReadFile (h, buf, count, &r, &ovl)) {
    if (GetLastError () == ERROR_HANDLE_EOF)
      return 0;
    nbdkit_debug ("ReadFile: error %d", GetLastError ());
    errno = EIO;
    return -1;
  }

  return r;
}

#else /* !WIN32 */
#error "no replacement pread is available on this platform"
#endif

#endif /* !HAVE_PREAD */
