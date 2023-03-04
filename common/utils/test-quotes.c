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

/* Unit tests of utils quoting code. */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG /* Keep test strong even for nbdkit built without assertions */
#include <assert.h>
#include <stdbool.h>

#include "array-size.h"
#include "utils.h"

#ifdef HAVE_OPEN_MEMSTREAM

static bool
test (const char *orig, const char *fnname, void (*fn) (const char *, FILE *),
      const char *exp)
{
  char *str = NULL;
  size_t str_len = 0;
  FILE *fp;

  fp = open_memstream (&str, &str_len);
  assert (fp);
  fn (orig, fp);
  if (fclose (fp) == EOF)
    assert (false);
  if (str_len == 0 && !str)
    str = strdup ("");
  assert (str);

  if (strcmp (str, exp)) {
    fprintf (stderr, "%s failed, got '%s' expected '%s'\n",
             fnname, str, exp);
    free (str);
    return true;
  }
  free (str);
  return false;
}

int
main (void)
{
  struct {
    const char *orig;
    const char *shell;
    const char *uri;
  } tests[] = {
    { "a-b_c.0", "a-b_c.0", "a-b_c.0" },
    { "/Safe/Path", "/Safe/Path", "/Safe/Path" },
    { "a~b", "\"a~b\"", "a~b" },
    { "", "\"\"", "" },
    { "a=b", "a=b", "a%3Db" }, /* XXX shell wrong if used as argv[0] */
    { "a;b", "\"a;b\"", "a%3Bb" },
    { "a b", "\"a b\"", "a%20b" },
    { "a%b", "\"a%b\"", "a%25b" },
    { "a'b\"c$d`e\\f", "\"a'b\\\"c\\$d\\`e\\\\f\"", "a%27b%22c%24d%60e%5Cf" },
  };
  size_t i;
  bool fail = false;

  for (i = 0; i < ARRAY_SIZE (tests); i++) {
    fail |= test (tests[i].orig, "shell_quote", shell_quote, tests[i].shell);
    fail |= test (tests[i].orig, "uri_quote", uri_quote, tests[i].uri);
  }
  return fail ? EXIT_FAILURE : EXIT_SUCCESS;
}

#else /* !OPEN_MEMSTREAM */

int
main (int argc, char *argv[])
{
  fprintf (stderr, "%s: test skipped because no support for open_memstream\n",
           argv[0]);
  exit (77);
}

#endif /* !OPEN_MEMSTREAM */
