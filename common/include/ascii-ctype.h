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

/* Normal ctype functions are affected by the current locale.  For
 * example isupper() might recognize Ä in some but not all locales.
 * These functions match only 7 bit ASCII characters.
 */

#ifndef NBDKIT_ASCII_CTYPE_H
#define NBDKIT_ASCII_CTYPE_H

#define ascii_isalnum(c) (ascii_isalpha (c) || ascii_isdigit (c))

#define ascii_isalpha(c)                                        \
  (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))

#define ascii_isdigit(c)                        \
  ((c) >= '0' && (c) <= '9')

#define ascii_isspace(c)                                                \
  ((c) == '\t' || (c) == '\n' || (c) == '\f' || (c) == '\r' || (c) == ' ')

#define ascii_isupper(c)                        \
  ((c) >= 'A' && (c) <= 'Z')

#define ascii_islower(c)                        \
  ((c) >= 'a' && (c) <= 'z')

/* See also hexdigit.h */
#define ascii_isxdigit(c)                                               \
  ((c) == '0' || (c) == '1' || (c) == '2' || (c) == '3' || (c) == '4' || \
   (c) == '5' || (c) == '6' || (c) == '7' || (c) == '8' || (c) == '9' || \
   (c) == 'a' || (c) == 'b' || (c) == 'c' ||                            \
   (c) == 'd' || (c) == 'e' || (c) == 'f' ||                            \
   (c) == 'A' || (c) == 'B' || (c) == 'C' ||                            \
   (c) == 'D' || (c) == 'E' || (c) == 'F')

#define ascii_tolower(c)                        \
  (ascii_isupper ((c)) ? (c) + 32 : (c))

#define ascii_toupper(c)                        \
  (ascii_islower ((c)) ? (c) - 32 : (c))

#define ascii_isprint(c) ((c) >= 32 && (c) <= 126)

#endif /* NBDKIT_ASCII_CTYPE_H */
