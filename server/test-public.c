/* nbdkit
 * Copyright (C) 2018-2020 Red Hat Inc.
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
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "internal.h"

static bool error_flagged;

/* Stubs for linking against minimal source files, and for proving
 * that an error message is issued when expected.
 */
void
nbdkit_error (const char *fs, ...)
{
  error_flagged = true;
}

void
nbdkit_debug (const char *fs, ...)
{
}

bool listen_stdin;
bool configured;

volatile int quit;
#ifndef WIN32
int quit_fd = -1;
#else
extern HANDLE quit_fd;
#endif

struct connection *
threadlocal_get_conn (void)
{
  abort ();
}

int connection_get_status (void)
{
  abort ();
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

static bool
test_nbdkit_parse_ints (void)
{
  bool pass = true;

#define PARSE(...) PARSE_(__VA_ARGS__)
#define PARSE_(TYPE, FORMAT, TEST, RET, EXPECTED)                       \
  do {                                                                  \
    error_flagged = false;                                              \
    TYPE i = 123;                                                       \
    int r = nbdkit_parse_##TYPE ("test", TEST, &i);                     \
    if (r != RET || i != (r ? 123 : EXPECTED)) {                        \
      fprintf (stderr,                                                  \
               "%s: %d: wrong parse for %s: r=%d i=" FORMAT "\n",       \
               __FILE__, __LINE__, TEST, r, i);                         \
      pass = false;                                                     \
    }                                                                   \
    if ((r == -1) != error_flagged) {                                   \
      fprintf (stderr,                                                  \
               "%s: %d: wrong error message handling for %s\n",         \
               __FILE__, __LINE__, TEST);                               \
      pass = false;                                                     \
    }                                                                   \
  } while (0)
#define OK 0
#define BAD -1, 0

  /* Test the basic parsing of decimals, hexadecimal, octal and
   * negative numbers.
   */
  PARSE (int, "%d", "0",    OK, 0);
  PARSE (int, "%d", " 0",   OK, 0);
  PARSE (int, "%d", "  0",  OK, 0);
  PARSE (int, "%d", "   0", OK, 0);
  PARSE (int, "%d", "1",    OK, 1);
  PARSE (int, "%d", " 1",   OK, 1);
  PARSE (int, "%d", "  1",  OK, 1);
  PARSE (int, "%d", "   1", OK, 1);
  PARSE (int, "%d", "99",   OK, 99);
  PARSE (int, "%d", "0x1",  OK, 1);
  PARSE (int, "%d", "0xf",  OK, 15);
  PARSE (int, "%d", "0x10", OK, 16);
  PARSE (int, "%d", "0xff", OK, 255);
  PARSE (int, "%d", "0Xff", OK, 255);
  PARSE (int, "%d", "01",   OK, 1);
  PARSE (int, "%d", "07",   OK, 7);
  PARSE (int, "%d", "010",  OK, 8);
  PARSE (int, "%d", "+0",   OK, 0);
  PARSE (int, "%d", " +0",  OK, 0);
  PARSE (int, "%d", "+99",  OK, 99);
  PARSE (int, "%d", "+0xf", OK, 15);
  PARSE (int, "%d", "+010", OK, 8);
  PARSE (int, "%d", "-0",   OK, 0);
  PARSE (int, "%d", " -0",  OK, 0);
  PARSE (int, "%d", "  -0", OK, 0);
  PARSE (int, "%d", "-99",  OK, -99);
  PARSE (int, "%d", "-0xf", OK, -15);
  PARSE (int, "%d", "-0XF", OK, -15);
  PARSE (int, "%d", "-010", OK, -8);
  PARSE (int, "%d", "2147483647", OK, 2147483647); /* INT_MAX */
  PARSE (int, "%d", "-2147483648", OK, -2147483648); /* INT_MIN */
  PARSE (int, "%d", "0x7fffffff", OK, 0x7fffffff);
  PARSE (int, "%d", "-0x80000000", OK, -0x80000000);

  /* Test basic error handling. */
  PARSE (int, "%d", "",        BAD);
  PARSE (int, "%d", "-",       BAD);
  PARSE (int, "%d", "- 0",     BAD);
  PARSE (int, "%d", "+",       BAD);
  PARSE (int, "%d", "++",      BAD);
  PARSE (int, "%d", "++0",     BAD);
  PARSE (int, "%d", "--0",     BAD);
  PARSE (int, "%d", "0x",      BAD);
  PARSE (int, "%d", "0xg",     BAD);
  PARSE (int, "%d", "08",      BAD);
  PARSE (int, "%d", "0x1p1",   BAD);
  PARSE (int, "%d", "42x",     BAD);
  PARSE (int, "%d", "42e42",   BAD);
  PARSE (int, "%d", "42-",     BAD);
  PARSE (int, "%d", "garbage", BAD);
  PARSE (int, "%d", "inf",     BAD);
  PARSE (int, "%d", "nan",     BAD);
  PARSE (int, "%d", "0.0",     BAD);
  PARSE (int, "%d", "1,000",   BAD);
  PARSE (int, "%d", "2147483648", BAD); /* INT_MAX + 1 */
  PARSE (int, "%d", "-2147483649", BAD); /* INT_MIN - 1 */
  PARSE (int, "%d", "999999999999999999999999", BAD);
  PARSE (int, "%d", "-999999999999999999999999", BAD);

  /* Test nbdkit_parse_unsigned. */
  PARSE (unsigned, "%u", "0",    OK, 0);
  PARSE (unsigned, "%u", " 0",   OK, 0);
  PARSE (unsigned, "%u", "1",    OK, 1);
  PARSE (unsigned, "%u", "99",   OK, 99);
  PARSE (unsigned, "%u", "0x1",  OK, 1);
  PARSE (unsigned, "%u", "0xf",  OK, 15);
  PARSE (unsigned, "%u", "0x10", OK, 16);
  PARSE (unsigned, "%u", "0xff", OK, 255);
  PARSE (unsigned, "%u", "01",   OK, 1);
  PARSE (unsigned, "%u", "07",   OK, 7);
  PARSE (unsigned, "%u", "010",  OK, 8);
  PARSE (unsigned, "%u", "+0",   OK, 0);
  PARSE (unsigned, "%u", "+99",  OK, 99);
  PARSE (unsigned, "%u", "+0xf", OK, 15);
  PARSE (unsigned, "%u", "+010", OK, 8);
  PARSE (unsigned, "%u", "-0",   BAD); /* this is by choice */
  PARSE (unsigned, "%u", " -0",  BAD);
  PARSE (unsigned, "%u", "-99",  BAD);
  PARSE (unsigned, "%u", "-0xf", BAD);
  PARSE (unsigned, "%u", "-010", BAD);
  PARSE (unsigned, "%u", "2147483647", OK, 2147483647); /* INT_MAX */
  PARSE (unsigned, "%u", "-2147483648", BAD); /* INT_MIN */
  PARSE (unsigned, "%u", "0x7fffffff", OK, 0x7fffffff);
  PARSE (unsigned, "%u", "-0x80000000", BAD);

  /* Test nbdkit_parse_int8_t. */
  PARSE (int8_t, "%" PRIi8, "0",     OK, 0);
  PARSE (int8_t, "%" PRIi8, "0x7f",  OK, 0x7f);
  PARSE (int8_t, "%" PRIi8, "-0x80", OK, -0x80);
  PARSE (int8_t, "%" PRIi8, "0x80",  BAD);
  PARSE (int8_t, "%" PRIi8, "-0x81", BAD);

  /* Test nbdkit_parse_uint8_t. */
  PARSE (uint8_t, "%" PRIu8, "0",     OK, 0);
  PARSE (uint8_t, "%" PRIu8, "0xff",  OK, 0xff);
  PARSE (uint8_t, "%" PRIu8, "0x100", BAD);
  PARSE (uint8_t, "%" PRIu8, "-1",    BAD);

  /* Test nbdkit_parse_int16_t. */
  PARSE (int16_t, "%" PRIi16, "0",       OK, 0);
  PARSE (int16_t, "%" PRIi16, "0x7fff",  OK, 0x7fff);
  PARSE (int16_t, "%" PRIi16, "-0x8000", OK, -0x8000);
  PARSE (int16_t, "%" PRIi16, "0x8000",  BAD);
  PARSE (int16_t, "%" PRIi16, "-0x8001", BAD);

  /* Test nbdkit_parse_uint16_t. */
  PARSE (uint16_t, "%" PRIu16, "0",       OK, 0);
  PARSE (uint16_t, "%" PRIu16, "0xffff",  OK, 0xffff);
  PARSE (uint16_t, "%" PRIu16, "0x10000", BAD);
  PARSE (uint16_t, "%" PRIu16, "-1",      BAD);

  /* Test nbdkit_parse_int32_t. */
  PARSE (int32_t, "%" PRIi32, "0",           OK, 0);
  PARSE (int32_t, "%" PRIi32, "0x7fffffff",  OK, 0x7fffffff);
  PARSE (int32_t, "%" PRIi32, "-0x80000000", OK, -0x80000000);
  PARSE (int32_t, "%" PRIi32, "0x80000000",  BAD);
  PARSE (int32_t, "%" PRIi32, "-0x80000001", BAD);

  /* Test nbdkit_parse_uint32_t. */
  PARSE (uint32_t, "%" PRIu32, "0",           OK, 0);
  PARSE (uint32_t, "%" PRIu32, "0xffffffff",  OK, 0xffffffff);
  PARSE (uint32_t, "%" PRIu32, "0x100000000", BAD);
  PARSE (uint32_t, "%" PRIu32, "-1",          BAD);

  /* Test nbdkit_parse_int64_t. */
  PARSE (int64_t, "%" PRIi64, "0",                   OK, 0);
  PARSE (int64_t, "%" PRIi64, "0x7fffffffffffffff",  OK, 0x7fffffffffffffff);
  PARSE (int64_t, "%" PRIi64, "-0x8000000000000000", OK, -0x8000000000000000);
  PARSE (int64_t, "%" PRIi64, "0x8000000000000000",  BAD);
  PARSE (int64_t, "%" PRIi64, "-0x8000000000000001", BAD);

  /* Test nbdkit_parse_uint64_t. */
  PARSE (uint64_t, "%" PRIu64, "0",                   OK, 0);
  PARSE (uint64_t, "%" PRIu64, "0xffffffffffffffff",  OK, 0xffffffffffffffff);
  PARSE (uint64_t, "%" PRIu64, "0x10000000000000000", BAD);
  PARSE (uint64_t, "%" PRIu64, "-1",                  BAD);

#undef PARSE
#undef PARSE_
#undef OK
#undef BAD
  return pass;
}

static bool
test_nbdkit_read_password (void)
{
  bool pass = true;
  char template[] = "+/tmp/nbdkit_testpw_XXXXXX";
#ifndef WIN32
  char template2[] = "/tmp/nbdkit_testpw2_XXXXXX";
  char fdbuf[16];
#endif
  char *pw = template;
  int fd;

  /* Test expected failure - no such file */
  error_flagged = false;
  if (nbdkit_read_password ("+/nosuch", &pw) != -1) {
    fprintf (stderr, "Failed to diagnose failed password file\n");
    pass = false;
  }
  else if (pw != NULL) {
    fprintf (stderr, "Failed to set password to NULL on failure\n");
    pass = false;
  }
  else if (!error_flagged) {
    fprintf (stderr, "Wrong error message handling\n");
    pass = false;
  }
  error_flagged = false;

  /* Test direct password */
  if (nbdkit_read_password ("abc", &pw) != 0) {
    fprintf (stderr, "Failed to reuse direct password\n");
    pass = false;
  }
  else if (strcmp (pw, "abc") != 0) {
    fprintf (stderr, "Wrong direct password, expected 'abc' got '%s'\n", pw);
    pass = false;
  }
  free (pw);
  pw = NULL;

  /* Test reading password from file */
  fd = mkstemp (&template[1]);
  if (fd < 0) {
    perror ("mkstemp");
    pass = false;
  }
  else if (write (fd, "abc\n", 4) != 4 ||
           /* NB: On Windows we must close the temporary file here.
            * This is because nbdkit_read_password will try to open a
            * second file descriptor on this file, which will fail
            * with ERROR_SHARING_VIOLATION because Windows is stupid.
            */
           close (fd) == -1) {
    fprintf (stderr, "Failed to write to file %s\n", &template[1]);
    pass = false;
  }
  else if (nbdkit_read_password (template, &pw) != 0) {
    fprintf (stderr, "Failed to read password from file %s\n", &template[1]);
    pass = false;
  }
  else if (strcmp (pw, "abc") != 0) {
    fprintf (stderr, "Wrong file password, expected 'abc' got '%s'\n", pw);
    pass = false;
  }
  free (pw);
  unlink (&template[1]);

#ifndef WIN32
  /* Test reading password from file descriptor. */
  fd = mkstemp (template2);
  if (fd < 0) {
    perror ("mkstemp");
    pass = false;
  }
  else if (write (fd, "abc\n", 4) != 4) {
    fprintf (stderr, "Failed to write to file %s\n", template2);
    pass = false;
  }
  else {
    snprintf (fdbuf, sizeof fdbuf, "-%d", fd);
    lseek (fd, 0, 0);
    if (nbdkit_read_password (fdbuf, &pw) == -1) {
      fprintf (stderr, "Failed to read password from fd %s\n", fdbuf);
      pass = false;
    }
    else if (strcmp (pw, "abc") != 0) {
      fprintf (stderr, "Wrong file password, expected 'abc' got '%s'\n", pw);
      pass = false;
    }
    free (pw);
  }

  if (fd >= 0) {
    /* Don't close fd, it is closed by nbdkit_read_password. */
    unlink (template2);
  }

  if (error_flagged) {
    fprintf (stderr, "Wrong error message handling\n");
    pass = false;
  }
#endif /* !WIN32 */

  /* XXX Testing reading from stdin would require setting up a pty. But
   * we can test that it is forbidden with -s.
   */
  listen_stdin = true;
  if (nbdkit_read_password ("-", &pw) != -1) {
    fprintf (stderr, "Failed to diagnose failed password from stdin with -s\n");
    pass = false;
  }
  else if (pw != NULL) {
    fprintf (stderr, "Failed to set password to NULL on failure\n");
    pass = false;
  }
  else if (!error_flagged) {
    fprintf (stderr, "Wrong error message handling\n");
    pass = false;
  }
  error_flagged = false;

  return pass;
}

int
main (int argc, char *argv[])
{
  bool pass = true;
  pass &= test_nbdkit_parse_size ();
  pass &= test_nbdkit_parse_ints ();
  pass &= test_nbdkit_read_password ();
  /* nbdkit_absolute_path and nbdkit_nanosleep not unit-tested here, but
   * get plenty of coverage in the main testsuite.
   */
  return pass ? EXIT_SUCCESS : EXIT_FAILURE;
}
