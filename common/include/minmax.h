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

#ifndef NBDKIT_MINMAX_H
#define NBDKIT_MINMAX_H

#include <config.h>

/* Safe MIN and MAX macros.  These evaluate each parameter once.  They
 * work for any comparable type.
 *
 * __auto_type is a GCC extension, added in GCC 4.9 in 2014, and to
 * Clang in 2015.  The fallback uses typeof which was added a very
 * long time ago.  Note that OpenBSD lacks __auto_type so the fallback
 * is required.
 */

#include "unique-name.h"

#undef MIN
#define MIN(x, y) \
  MIN_1 ((x), (y), NBDKIT_UNIQUE_NAME (_x), NBDKIT_UNIQUE_NAME (_y))
#undef MAX
#define MAX(x, y) \
  MAX_1 ((x), (y), NBDKIT_UNIQUE_NAME (_x), NBDKIT_UNIQUE_NAME (_y))

#ifdef HAVE_AUTO_TYPE

#define MIN_1(x, y, _x, _y) ({                 \
      __auto_type _x = (x);                    \
      __auto_type _y = (y);                    \
      _x < _y ? _x : _y;                       \
    })
#define MAX_1(x, y, _x, _y) ({                 \
      __auto_type _x = (x);                    \
      __auto_type _y = (y);                    \
      _x > _y ? _x : _y;                       \
    })

#else

#define MIN_1(x, y, _x, _y) ({                 \
      typeof (x) _x = (x);                     \
      typeof (y) _y = (y);                     \
      _x < _y ? _x : _y;                       \
    })
#define MAX_1(x, y, _x, _y) ({                 \
      typeof (x) _x = (x);                     \
      typeof (y) _y = (y);                     \
      _x > _y ? _x : _y;                       \
    })

#endif

#endif /* NBDKIT_MINMAX_H */
