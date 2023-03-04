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

/* Case insensitive string comparison functions (like strcasecmp,
 * strncasecmp) which work correctly in any locale.  They can only be
 * used for comparison when one or both strings is 7 bit ASCII.
 */

#ifndef NBDKIT_ASCII_STRING_H
#define NBDKIT_ASCII_STRING_H

#include "ascii-ctype.h"

static inline int
ascii_strcasecmp (const char *s1, const char *s2)
{
  const unsigned char *us1 = (const unsigned char *)s1;
  const unsigned char *us2 = (const unsigned char *)s2;

  while (ascii_tolower (*us1) == ascii_tolower (*us2)) {
    if (*us1++ == '\0')
      return 0;
    us2++;
  }

  return ascii_tolower (*us1) - ascii_tolower (*us2);
}

static inline int
ascii_strncasecmp (const char *s1, const char *s2, size_t n)
{
  if (n != 0) {
    const unsigned char *us1 = (const unsigned char *)s1;
    const unsigned char *us2 = (const unsigned char *)s2;

    do {
      if (ascii_tolower (*us1) != ascii_tolower (*us2))
        return ascii_tolower (*us1) - ascii_tolower (*us2);
      if (*us1++ == '\0')
        break;
      us2++;
    } while (--n != 0);
  }

  return 0;
}

#endif /* NBDKIT_ASCII_STRING_H */
