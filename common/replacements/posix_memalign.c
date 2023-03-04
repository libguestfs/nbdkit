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
#include <errno.h>

#ifndef HAVE_POSIX_MEMALIGN

#include "posix_memalign.h"

#ifdef HAVE_VALLOC

int
posix_memalign (void **ptr, size_t alignment, size_t size)
{
  *ptr = valloc (size);
  if (*ptr == NULL)
    return errno;
  return 0;
}

#else /* !HAVE_VALLOC */

#ifdef WIN32

#include <memoryapi.h>

int
posix_memalign (void **ptr, size_t alignment, size_t size)
{
  *ptr = VirtualAlloc (NULL, size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
  if (*ptr == NULL)
    return ENOMEM;
  return 0;
}

#else /* !WIN32 */
#error "no replacement posix_memalign() is available on this platform"
#endif

#endif /* !HAVE_VALLOC */

#endif /* !HAVE_POSIX_MEMALIGN */
