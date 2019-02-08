/* nbdkit
 * Copyright (C) 2018 Red Hat Inc.
 * All rights reserved.
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
#include <inttypes.h>
#include <stdbool.h>

#include "internal.h"

static bool error_flagged;

/* Stub for linking against minimal source files, and for proving that
 * an error message is issued when expected.  */
void
nbdkit_error (const char *fs, ...)
{
  error_flagged = true;
}

static bool
test_nbdkit_parse_size (void)
{
  bool pass = true;
  struct pair {
    const char *str;
    int64_t res;
  } pairs[] = {
    /* Bogus strings */
    { "", -1 },
    { "0x0", -1 },
    { "garbage", -1 },
    { "0garbage", -1 },
    { "8E", -1 },
    { "8192P", -1 },

    /* Strings leading to overflow */
    { "9223372036854775808", -1 }, /* INT_MAX + 1 */
    { "18446744073709551614", -1 }, /* UINT64_MAX - 1 */
    { "18446744073709551615", -1 }, /* UINT64_MAX */
    { "18446744073709551616", -1 }, /* UINT64_MAX + 1 */
    { "999999999999999999999999", -1 },

    /* Strings representing negative values */
    { "-1", -1 },
    { "-2", -1 },
    { "-9223372036854775809", -1 }, /* INT64_MIN - 1 */
    { "-9223372036854775808", -1 }, /* INT64_MIN */
    { "-9223372036854775807", -1 }, /* INT64_MIN + 1 */
    { "-18446744073709551616", -1 }, /* -UINT64_MAX - 1 */
    { "-18446744073709551615", -1 }, /* -UINT64_MAX */
    { "-18446744073709551614", -1 }, /* -UINT64_MAX + 1 */

    /* Strings we may want to support in the future */
    { "M", -1 },
    { "1MB", -1 },
    { "1MiB", -1 },
    { "1.5M", -1 },

    /* Valid strings */
    { "-0", 0 },
    { "0", 0 },
    { "+0", 0 },
    { " 08", 8 },
    { "1", 1 },
    { "+1", 1 },    
    { "1234567890", 1234567890 },
    { "+1234567890", 1234567890 },    
    { "9223372036854775807", INT64_MAX },
    { "1s", 512 },
    { "2S", 1024 },
    { "1b", 1 },
    { "1B", 1 },
    { "1k", 1024 },
    { "1K", 1024 },
    { "1m", 1024 * 1024 },
    { "1M", 1024 * 1024 },
    { "+1M", 1024 * 1024 },    
    { "1g", 1024 * 1024 * 1024 },
    { "1G", 1024 * 1024 * 1024 },
    { "1t", 1024LL * 1024 * 1024 * 1024 },
    { "1T", 1024LL * 1024 * 1024 * 1024 },
    { "1p", 1024LL * 1024 * 1024 * 1024 * 1024 },
    { "1P", 1024LL * 1024 * 1024 * 1024 * 1024 },
    { "8191p", 1024LL * 1024 * 1024 * 1024 * 1024 * 8191 },
    { "1e", 1024LL * 1024 * 1024 * 1024 * 1024 * 1024 },
    { "1E", 1024LL * 1024 * 1024 * 1024 * 1024 * 1024 },
  };

  for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
    int64_t r;

    error_flagged = false;
    r = nbdkit_parse_size (pairs[i].str);
    if (r != pairs[i].res) {
      fprintf (stderr,
               "Wrong parse for %s, got %" PRId64 ", expected %" PRId64 "\n",
               pairs[i].str, r, pairs[i].res);
      pass = false;
    }
    if ((r == -1) != error_flagged) {
      fprintf (stderr, "Wrong error message handling for %s\n", pairs[i].str);
      pass = false;
    }
  }

  return pass;
}

int
main (int argc, char *argv[])
{
  bool pass = true;
  pass &= test_nbdkit_parse_size ();
  /* XXX add tests for nbdkit_absolute_path, nbdkit_read_password */
  return pass ? 0 : 1;
}
