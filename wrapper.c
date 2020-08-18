/* nbdkit
 * Copyright (C) 2017-2020 Red Hat Inc.
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

/*------------------------------------------------------------
 * This is not nbdkit.  This is a wrapper which lets you run nbdkit
 * from the source directory without installing nbdkit.
 *
 * You can use either:
 *
 *   ./nbdkit file [arg=value] [arg=value] ...
 *
 * or:
 *
 *   /path/to/nbdkit file [arg=value] [arg=value] ...
 *
 * or you can set $PATH to include the nbdkit source directory and run
 * the bare "nbdkit" command without supplying the full path.
 *
 * The wrapper modifies the bare plugin name (eg. "file") to be the
 * full path to the locally compiled plugin.  If you don't use this
 * program and run server/nbdkit directly then it will pick up the
 * globally installed plugins which is usually not what you want.
 *
 * This program is also used to run the tests (make check).
 *
 * You can enable valgrind by setting NBDKIT_VALGRIND=1 (this
 * is mainly used by the internal tests).
 *
 * You can enable debugging by setting NBDKIT_GDB=1
 *------------------------------------------------------------
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <time.h>

#include "options.h"
#include "windows-compat.h"
#include "utils.h"

/* Construct an array of parameters passed through to real nbdkit. */
static const char **cmd;
static size_t len;

/* Plugins written in scripting languages (only Perl right now) need
 * to be rewritten on the command line in a different way from plugins
 * written in C.  So we have to list those here.
 */
static bool
is_perl_plugin (const char *name)
{
  return strcmp (name, "example4") == 0;
}

static void
passthru (const char *s)
{
  cmd = realloc (cmd, (len+1) * sizeof (const char *));
  if (cmd == NULL)
    abort ();
  cmd[len] = s;
  ++len;
}

static void __attribute__((format (printf, 1, 2)))
passthru_format (const char *fs, ...)
{
  va_list args;
  char *str;

  va_start (args, fs);
  if (vasprintf (&str, fs, args) == -1)
    abort ();
  va_end (args);
  passthru (str);
}

static void
end_passthru (void)
{
  passthru (NULL);
}

static void
print_command (void)
{
  size_t i;

  if (len > 0)
    shell_quote (cmd[0], stderr);
  for (i = 1; i < len && cmd[i] != NULL; ++i) {
    fputc (' ', stderr);
    shell_quote (cmd[i], stderr);
  }
  fputc ('\n', stderr);
}

#ifdef WIN32
/* Windows behaviour of _spawnvp is completely insane:
 * https://stackoverflow.com/questions/4146980/how-to-avoid-space-splitting-and-quote-removal-with-spawnvp
 */
static const char *
quote_string_for_spawn (const char *str)
{
  size_t i, len;
  char *p, *ret = (char *) str;

  if (strchr (str, ' ') || strchr (str, '"')) {
    len = strlen (str);

    p = ret = malloc (2 + len*2 + 1);
    if (ret == NULL) {
      perror ("malloc");
      exit (EXIT_FAILURE);
    }

    *p++ = '"';
    for (i = 0; i < len; ++i) {
      switch (str[i]) {
      case '"':
      case '\\':
        *p++ = '\\';
        *p++ = str[i];
        break;
      default:
        *p++ = str[i];
      }
    }
    *p++ = '"';
    *p++ = '\0';
  }

  /* We never free these strings. */
  return ret;
}
#endif /* WIN32 */

int
main (int argc, char *argv[])
{
  bool verbose = false;
  char *s;
  time_t t;
  unsigned tu;
  char ts[32];
  int r;

#ifndef WIN32
  /* If NBDKIT_VALGRIND=1 is set in the environment, then we run the
   * program under valgrind.  This is used by the tests.  Similarly if
   * NBDKIT_GDB=1 is set, we run the program under GDB, useful during
   * development.
   */
  s = getenv ("NBDKIT_VALGRIND");
  if (s && strcmp (s, "1") == 0) {
    passthru (VALGRIND);
    passthru ("--vgdb=no");
    passthru ("--leak-check=full");
    passthru ("--show-leak-kinds=all");
    passthru ("--error-exitcode=119");
    passthru_format ("--suppressions=%s/valgrind/suppressions", builddir);
    passthru ("--trace-children=no");
    passthru ("--run-libc-freeres=no");
    passthru ("--num-callers=100");
    /* This is a temporary workaround until RHBZ#1662656 is fixed: */
    passthru ("--read-inline-info=no");
  }
  else {
    s = getenv ("NBDKIT_GDB");
    if (s && strcmp (s, "1") == 0) {
      passthru ("gdb");
      passthru ("--args");
    }
  }
#endif

  /* Needed for plugins written in OCaml. */
#ifndef WIN32
#define LD_LIBRARY_PATH "LD_LIBRARY_PATH"
#else
#define LD_LIBRARY_PATH "PATH"
#endif
  s = getenv (LD_LIBRARY_PATH);
  if (s)
    r = asprintf (&s, "%s/plugins/ocaml/.libs:%s", builddir, s);
  else
    r = asprintf (&s, "%s/plugins/ocaml/.libs", builddir);
  if (r < 0) {
    perror ("asprintf");
    exit (EXIT_FAILURE);
  }
  setenv (LD_LIBRARY_PATH, s, 1);
  free (s);
  s = getenv ("LIBRARY_PATH");
  if (s)
    r = asprintf (&s, "%s/plugins/ocaml/.libs:%s", builddir, s);
  else
    r = asprintf (&s, "%s/plugins/ocaml/.libs", builddir);
  if (r < 0) {
    perror ("asprintf");
    exit (EXIT_FAILURE);
  }
  setenv ("LIBRARY_PATH", s, 1);
  free (s);

  /* Absolute path of the real nbdkit command. */
  passthru_format ("%s/server/nbdkit" EXEEXT, builddir);

  /* Option parsing.  We don't really parse options here.  We are only
   * interested in which options have arguments and which need
   * rewriting.
   */
  for (;;) {
    int c;
    int long_index = -1;
    bool is_long_option;

    c = getopt_long (argc, argv, short_options, long_options, &long_index);
    if (c == -1)
      break;

    if (c == '?')               /* getopt prints an error */
      exit (EXIT_FAILURE);

    /* long_index is only set if it's an actual long option. */
    is_long_option = long_index >= 0;

    /* Verbose is special because we will print the final command. */
    if (c == 'v') {
      verbose = true;
      if (is_long_option)
        passthru ("--verbose");
      else
        passthru ("-v");
    }
    /* Filters can be rewritten if they are a short name. */
    else if (c == FILTER_OPTION) {
      if (is_short_name (optarg))
        passthru_format ("--filter="
                         "%s/filters/%s/.libs/nbdkit-%s-filter." SOEXT,
                         builddir, optarg, optarg);
      else
        passthru_format ("--filter=%s", optarg);
    }
    /* Any long option. */
    else if (is_long_option) {
      if (optarg)           /* Long option which takes an argument. */
        passthru_format ("--%s=%s", long_options[long_index].name, optarg);
      else                  /* Long option which takes no argument. */
        passthru_format ("--%s", long_options[long_index].name);
    }
    /* Any short option. */
    else {
      passthru_format ("-%c", c);
      if (optarg)
        passthru (optarg);
    }
  }

  /* Are there any non-option arguments? */
  if (optind < argc) {
    /* Ensure any further parameters can never be parsed as options by
     * real nbdkit.
     */
    passthru ("--");

    /* The first non-option argument is the plugin name.  If it is a
     * short name then rewrite it.
     */
    if (is_short_name (argv[optind])) {
      /* Special plugins written in Perl. */
      if (is_perl_plugin (argv[optind])) {
        passthru_format ("%s/plugins/perl/.libs/nbdkit-perl-plugin." SOEXT,
                         builddir);
        passthru_format ("%s/plugins/%s/nbdkit-%s-plugin",
                         builddir, argv[optind], argv[optind]);
      }
      else {
        passthru_format ("%s/plugins/%s/.libs/nbdkit-%s-plugin." SOEXT,
                         builddir, argv[optind], argv[optind]);
      }
      ++optind;
    }

    /* Everything else is passed through without rewriting. */
    while (optind < argc) {
      passthru (argv[optind]);
      ++optind;
    }
  }

  end_passthru ();
  if (verbose)
    print_command ();

  /* This is a cheap way to find some use-after-free and uninitialized
   * read problems when using glibc, and doesn't affect normal
   * operation or other libc.  We don't overwrite existing values so
   * this can be disabled or overridden at runtime.
   */
  setenv ("MALLOC_CHECK_", "1", 0);
  time (&t);
  tu = t;
  tu %= 255;
  tu++;
  snprintf (ts, sizeof ts, "%u", tu);
  setenv ("MALLOC_PERTURB_", ts, 0);

  /* Run the final command. */
#ifndef WIN32
  execvp (cmd[0], (char **) cmd);
  perror (cmd[0]);
  exit (EXIT_FAILURE);
#else /* WIN32 */
  size_t i;
  for (i = 1; cmd[i] != NULL; ++i)
    cmd[i] = quote_string_for_spawn (cmd[i]);
  r = _spawnvp (_P_WAIT, cmd[0], cmd);
  if (r == -1) {
    perror (cmd[0]);
    exit (EXIT_FAILURE);
  }
  exit (r == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
#endif /* WIN32 */
}
