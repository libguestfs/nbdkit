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
#undef NDEBUG /* Keep test strong even for nbdkit built without assertions */
#include <assert.h>

#include "ascii-ctype.h"

int
main (void)
{
  assert (ascii_isspace (' '));
  assert (ascii_isspace ('\t'));
  assert (ascii_isspace ('\n'));
  assert (! ascii_isspace ('a'));

  assert (ascii_isalpha ('a'));
  assert (ascii_isalpha ('Z'));
  assert (ascii_isalpha ('z'));
  assert (! ascii_isalpha (' '));
  assert (! ascii_isalpha ('0'));
  { const char *s = "Ä"; assert (! ascii_isalpha (s[0])); }
  { const char *s = "®"; assert (! ascii_isalpha (s[0])); }

  assert (ascii_isdigit ('0'));
  assert (ascii_isdigit ('9'));
  { const char *s = "Ø"; assert (! ascii_isdigit (s[0])); } /* U+00D8 */
  { const char *s = "９"; assert (! ascii_isdigit (s[0])); } /* U+FF19 */

  assert (ascii_islower ('a'));
  assert (ascii_islower ('z'));
  assert (! ascii_islower ('Z'));
  { const char *s = "Ä"; assert (! ascii_islower (s[0])); }

  assert (ascii_isupper ('A'));
  assert (ascii_isupper ('Z'));
  assert (! ascii_isupper ('z'));
  { const char *s = "Ä"; assert (! ascii_isupper (s[0])); }

  assert (ascii_tolower ('A') == 'a');
  assert (ascii_tolower ('Z') == 'z');
  assert (ascii_tolower ('a') == 'a');
  assert (ascii_tolower ('z') == 'z');
  assert (ascii_tolower ('0') == '0');
  { const char *s = "Ä"; assert (ascii_tolower (s[0]) == s[0]); }

  assert (ascii_toupper ('a') == 'A');
  assert (ascii_toupper ('z') == 'Z');
  assert (ascii_toupper ('A') == 'A');
  assert (ascii_toupper ('Z') == 'Z');
  assert (ascii_toupper ('0') == '0');
  { const char *s = "à"; assert (ascii_toupper (s[0]) == s[0]); }

  exit (EXIT_SUCCESS);
}
