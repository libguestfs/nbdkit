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

/* The job of this header is to define macros (or functions) called
 * things like 'htobe32' and 'le64toh' which byte swap N-bit integers
 * between host representation, and little and big endian.  Also
 * bswap_16, bswap_32 and bswap_64 which simply swap endianness.
 *
 * The core code and plugins in nbdkit uses these names and relies on
 * this header to provide the platform-specific implementation.  On
 * GNU/Linux these are defined in <endian.h> and <byteswap.h> but
 * other platforms have other requirements.
 */

#ifndef NBDKIT_BYTE_SWAPPING_H
#define NBDKIT_BYTE_SWAPPING_H

#ifdef HAVE_BYTESWAP_H
#include <byteswap.h>
#endif

#ifdef HAVE_ENDIAN_H
#include <endian.h>
#endif

#ifdef HAVE_SYS_ENDIAN_H
#include <sys/endian.h>
#endif

#ifdef __HAIKU__
#include <ByteOrder.h>
#define htobe16(x) B_HOST_TO_BENDIAN_INT16 (x)
#define htole16(x) B_HOST_TO_LENDIAN_INT16 (x)
#define be16toh(x) B_BENDIAN_TO_HOST_INT16 (x)
#define le16toh(x) B_LENDIAN_TO_HOST_INT16 (x)

#define htobe32(x) B_HOST_TO_BENDIAN_INT32 (x)
#define htole32(x) B_HOST_TO_LENDIAN_INT32 (x)
#define be32toh(x) B_BENDIAN_TO_HOST_INT32 (x)
#define le32toh(x) B_LENDIAN_TO_HOST_INT32 (x)

#define htobe64(x) B_HOST_TO_BENDIAN_INT64 (x)
#define htole64(x) B_HOST_TO_LENDIAN_INT64 (x)
#define be64toh(x) B_BENDIAN_TO_HOST_INT64 (x)
#define le64toh(x) B_LENDIAN_TO_HOST_INT64 (x)
#endif

/* If we didn't define bswap_16, bswap_32 and bswap_64 already above,
 * create macros.  GCC >= 4.8 and Clang have builtins.
 */
#ifndef bswap_16
#define bswap_16 __builtin_bswap16
#endif

#ifndef bswap_32
#define bswap_32 __builtin_bswap32
#endif

#ifndef bswap_64
#define bswap_64 __builtin_bswap64
#endif

/* If we didn't define htobe* above then define them in terms of
 * bswap_* macros.
 */
#ifndef htobe32
# ifndef WORDS_BIGENDIAN /* little endian */
#  define htobe16(x) bswap_16 (x)
#  define htole16(x) (x)
#  define be16toh(x) bswap_16 (x)
#  define le16toh(x) (x)

#  define htobe32(x) bswap_32 (x)
#  define htole32(x) (x)
#  define be32toh(x) bswap_32 (x)
#  define le32toh(x) (x)

#  define htobe64(x) bswap_64 (x)
#  define htole64(x) (x)
#  define be64toh(x) bswap_64 (x)
#  define le64toh(x) (x)

# else /* big endian */
#  define htobe16(x) (x)
#  define htole16(x) bswap_16 (x)
#  define be16toh(x) (x)
#  define le16toh(x) bswap_16 (x)

#  define htobe32(x) (x)
#  define htole32(x) bswap_32 (x)
#  define be32toh(x) (x)
#  define le32toh(x) bswap_32 (x)

#  define htobe64(x) (x)
#  define htole64(x) bswap_64 (x)
#  define be64toh(x) (x)
#  define le64toh(x) bswap_64 (x)
# endif
#endif

#endif /* NBDKIT_BYTE_SWAPPING_H */
