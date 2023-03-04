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

#include "ascii-string.h"

int
main (void)
{
  assert (ascii_strcasecmp ("", "") == 0);
  assert (ascii_strcasecmp ("a", "a") == 0);
  assert (ascii_strcasecmp ("abc", "abc") == 0);
  assert (ascii_strcasecmp ("a", "b") < 0);
  assert (ascii_strcasecmp ("b", "a") > 0);
  assert (ascii_strcasecmp ("aa", "a") > 0);

  /* Second string contains Turkish dotless lowercase letter ı. */
  assert (ascii_strcasecmp ("hi", "hı") != 0);

  /* Check that we got our rounding behaviour correct. */
  assert (ascii_strcasecmp ("\x1", "\x7f") < 0);
  assert (ascii_strcasecmp ("\x1", "\x80") < 0);
  assert (ascii_strcasecmp ("\x1", "\x81") < 0);
  assert (ascii_strcasecmp ("\x1", "\xff") < 0);

  assert (ascii_strncasecmp ("", "", 0) == 0);
  assert (ascii_strncasecmp ("a", "a", 1) == 0);
  assert (ascii_strncasecmp ("abc", "abc", 3) == 0);
  assert (ascii_strncasecmp ("abc", "def", 0) == 0);
  assert (ascii_strncasecmp ("abc", "abd", 2) == 0);
  assert (ascii_strncasecmp ("a", "b", 1) < 0);
  assert (ascii_strncasecmp ("b", "a", 1) > 0);
  assert (ascii_strncasecmp ("aa", "a", 2) > 0);
  assert (ascii_strncasecmp ("aa", "a", 100) > 0);

  assert (ascii_strncasecmp ("hi", "hı", 1) == 0);
  assert (ascii_strncasecmp ("hi", "hı", 2) != 0);

  assert (ascii_strncasecmp ("\x1", "\x7f", 1) < 0);
  assert (ascii_strncasecmp ("\x1", "\x80", 1) < 0);
  assert (ascii_strncasecmp ("\x1", "\x81", 1) < 0);
  assert (ascii_strncasecmp ("\x1", "\xff", 1) < 0);

  exit (EXIT_SUCCESS);
}
