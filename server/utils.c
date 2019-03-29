/* nbdkit
 * Copyright (C) 2013-2018 Red Hat Inc.
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
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <termios.h>
#include <errno.h>

#include "get-current-dir-name.h"

#include "internal.h"

char *
nbdkit_absolute_path (const char *path)
{
  CLEANUP_FREE char *pwd = NULL;
  char *ret;

  if (path == NULL || *path == '\0') {
    nbdkit_error ("cannot convert null or empty path to an absolute path");
    return NULL;
  }

  if (*path == '/') {
    ret = strdup (path);
    if (!ret) {
      nbdkit_error ("strdup: %m");
      return NULL;
    }
    return ret;
  }

  pwd = get_current_dir_name ();
  if (pwd == NULL) {
    nbdkit_error ("get_current_dir_name: %m");
    return NULL;
  }

  if (asprintf (&ret, "%s/%s", pwd, path) == -1) {
    nbdkit_error ("asprintf: %m");
    return NULL;
  }

  return ret;
}

/* Parse a string as a size with possible scaling suffix, or return -1
 * after reporting the error.
 */
int64_t
nbdkit_parse_size (const char *str)
{
  int64_t size;
  char *end;
  uint64_t scale = 1;

  /* Disk sizes cannot usefully exceed off_t (which is signed) and
   * cannot be negative.  */
  /* XXX Should we also parse things like '1.5M'? */
  /* XXX Should we allow hex? If so, hex cannot use scaling suffixes,
   * because some of them are valid hex digits */
  errno = 0;
  size = strtoimax (str, &end, 10);
  if (str == end) {
    nbdkit_error ("could not parse size string (%s)", str);
    return -1;
  }
  if (size < 0) {
    nbdkit_error ("size cannot be negative (%s)", str);
    return -1;
  }
  if (errno) {
    nbdkit_error ("size (%s) exceeds maximum value", str);
    return -1;
  }

  switch (*end) {
    /* No suffix */
  case '\0':
    end--; /* Safe, since we already filtered out empty string */
    break;

    /* Powers of 1024 */
  case 'e': case 'E':
    scale *= 1024;
    /* fallthru */
  case 'p': case 'P':
    scale *= 1024;
    /* fallthru */
  case 't': case 'T':
    scale *= 1024;
    /* fallthru */
  case 'g': case 'G':
    scale *= 1024;
    /* fallthru */
  case 'm': case 'M':
    scale *= 1024;
    /* fallthru */
  case 'k': case 'K':
    scale *= 1024;
    /* fallthru */
  case 'b': case 'B':
    break;

    /* "sectors", ie. units of 512 bytes, even if that's not the real
     * sector size */
  case 's': case 'S':
    scale = 512;
    break;

  default:
    nbdkit_error ("could not parse size: unknown suffix '%s'", end);
    return -1;
  }

  /* XXX Maybe we should support 'MiB' as a synonym for 'M'; and 'MB'
   * for powers of 1000, for similarity to GNU tools. But for now,
   * anything beyond 'M' is dropped.  */
  if (end[1]) {
    nbdkit_error ("could not parse size: unknown suffix '%s'", end);
    return -1;
  }

  if (INT64_MAX / scale < size) {
    nbdkit_error ("overflow computing size (%s)", str);
    return -1;
  }

  return size * scale;
}

/* Parse a string as a boolean, or return -1 after reporting the error.
 */
int
nbdkit_parse_bool (const char *str)
{
  if (!strcmp (str, "1") ||
      !strcasecmp (str, "true") ||
      !strcasecmp (str, "t") ||
      !strcasecmp (str, "yes") ||
      !strcasecmp (str, "y") ||
      !strcasecmp (str, "on"))
    return 1;

  if (!strcmp (str, "0") ||
      !strcasecmp (str, "false") ||
      !strcasecmp (str, "f") ||
      !strcasecmp (str, "no") ||
      !strcasecmp (str, "n") ||
      !strcasecmp (str, "off"))
    return 0;

  nbdkit_error ("could not decipher boolean (%s)", str);
  return -1;
}

/* Read a password from configuration value. */
int
nbdkit_read_password (const char *value, char **password)
{
  int tty, err;
  struct termios orig, temp;
  ssize_t r;
  size_t n;
  FILE *fp;

  /* Read from stdin. */
  if (strcmp (value, "-") == 0) {
    printf ("password: ");

    /* Set no echo. */
    tty = isatty (0);
    if (tty) {
      tcgetattr (0, &orig);
      temp = orig;
      temp.c_lflag &= ~ECHO;
      tcsetattr (0, TCSAFLUSH, &temp);
    }

    r = getline (password, &n, stdin);
    err = errno;

    /* Restore echo. */
    if (tty)
      tcsetattr (0, TCSAFLUSH, &orig);

    /* Complete the printf above. */
    printf ("\n");

    if (r == -1) {
      errno = err;
      nbdkit_error ("could not read password from stdin: %m");
      return -1;
    }
    if (*password && r > 0 && (*password)[r-1] == '\n')
      (*password)[r-1] = '\0';
  }

  /* Read password from a file. */
  else if (value[0] == '+') {
    fp = fopen (&value[1], "r");
    if (fp == NULL) {
      nbdkit_error ("open %s: %m", &value[1]);
      return -1;
    }
    r = getline (password, &n, fp);
    err = errno;
    fclose (fp);
    if (r == -1) {
      errno = err;
      nbdkit_error ("could not read password from file %s: %m", &value[1]);
      return -1;
    }
    if (*password && r > 0 && (*password)[r-1] == '\n')
      (*password)[r-1] = '\0';
  }

  /* Parameter is the password. */
  else {
    *password = strdup (value);
    if (*password == NULL) {
      nbdkit_error ("strdup: %m");
      return -1;
    }
  }

  return 0;
}

char *
nbdkit_realpath (const char *path)
{
  char *ret;

  if (path == NULL || *path == '\0') {
    nbdkit_error ("cannot resolve a null or empty path");
    return NULL;
  }

  ret = realpath (path, NULL);
  if (ret == NULL) {
    nbdkit_error ("realpath: %s: %m", path);
    return NULL;
  }

  return ret;
}
