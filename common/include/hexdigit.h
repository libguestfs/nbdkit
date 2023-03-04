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

#ifndef NBDKIT_HEXDIGIT_H
#define NBDKIT_HEXDIGIT_H

/* Convert a hex character ('0'-'9', 'a'-'f', 'A'-'F') to the
 * corresponding numeric value.
 *
 * Note this does *not* check that the input is a hex character.  To
 * check that, see ascii_isxdigit() in ascii-ctype.h
 */
static inline unsigned char
hexdigit (const char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  else if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  else /* if (c >= 'A' && c <= 'F') */
    return c - 'A' + 10;
}

/* Convert a pair of hex digits x0, x1 to a byte, eg. '5', 'A' -> 0x5A */
static inline unsigned char
hexbyte (const char x0, const char x1)
{
  unsigned char c0, c1;

  c0 = hexdigit (x0);
  c1 = hexdigit (x1);
  return c0 << 4 | c1;
}

/* Convert a number mod 16 to the corresponding hex character. */
static inline const char
hexchar (size_t i)
{
  static const char hex[] = "0123456789abcdef";

  return hex[i & 0xf];
}

#endif /* NBDKIT_HEXDIGIT_H */
