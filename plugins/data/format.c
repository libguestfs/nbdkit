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
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#define NBDKIT_API_VERSION 2

#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "sparse.h"
#include "format.h"

/* Store file at current offset in the sparse array, updating
 * the offset.
 */
static int
store_file (struct sparse_array *sa,
            const char *filename, int64_t *offset)
{
  FILE *fp;
  char buf[BUFSIZ];
  size_t n;

  fp = fopen (filename, "r");
  if (fp == NULL) {
    nbdkit_error ("%s: %m", filename);
    return -1;
  }

  while (!feof (fp)) {
    n = fread (buf, 1, BUFSIZ, fp);
    if (n > 0) {
      if (sparse_array_write (sa, buf, n, *offset) == -1) {
        fclose (fp);
        return -1;
      }
    }
    if (ferror (fp)) {
      nbdkit_error ("fread: %s: %m", filename);
      fclose (fp);
      return -1;
    }
    (*offset) += n;
  }

  if (fclose (fp) == EOF) {
    nbdkit_error ("fclose: %s: %m", filename);
    return -1;
  }

  return 0;
}

/* Parses the data parameter as described in the man page
 * under "DATA FORMAT".
 */
int
read_data_format (const char *value,
                  struct sparse_array *sa, int64_t *size)
{
  int64_t offset = 0;
  size_t i, len = strlen (value);

  for (i = 0; i < len; ++i) {
    int64_t j, k;
    int n;
    char c;

    switch (value[i]) {
    case '@':                   /* @OFFSET. */
      i++;
      if (sscanf (&value[i], "%" SCNi64 "%n", &j, &n) == 1) {
        if (j < 0) {
          nbdkit_error ("data parameter @OFFSET must not be negative");
          return -1;
        }
        i += n;
        offset = j;
      }
      else
        goto parse_error;
      break;

    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      /* BYTE*N */
      if (sscanf (&value[i], "%" SCNi64 "*%" SCNi64 "%n",
                  &j, &k, &n) == 2) {
        if (j < 0 || j > 255) {
          nbdkit_error ("data parameter BYTE must be in the range 0..255");
          return -1;
        }
        if (k < 0) {
          nbdkit_error ("data parameter *N must be >= 0");
          return -1;
        }
        i += n;

        c = j;
        while (k > 0) {
          if (sparse_array_write (sa, &c, 1, offset) == -1)
            return -1;
          offset++;
          k--;
        }
        if (*size < offset)
          *size = offset;
      }
      /* BYTE */
      else if (sscanf (&value[i], "%" SCNi64 "%n", &j, &n) == 1) {
        if (j < 0 || j > 255) {
          nbdkit_error ("data parameter BYTE must be in the range 0..255");
          return -1;
        }
        i += n;

        if (*size < offset+1)
          *size = offset+1;

        /* Store the byte. */
        c = j;
        if (sparse_array_write (sa, &c, 1, offset) == -1)
          return -1;
        offset++;
      }
      else
        goto parse_error;
      break;

    case '<': {                 /* <FILE */
      CLEANUP_FREE char *filename = NULL;
      size_t flen;

      i++;

      /* The filename follows next in the string. */
      flen = strcspn (&value[i], " \t\n");
      if (flen == 0) {
        nbdkit_error ("data parameter <FILE not a filename");
        return -1;
      }
      filename = strndup (&value[i], flen);
      if (filename == NULL) {
        nbdkit_error ("strndup: %m");
        return -1;
      }
      i += len;

      if (store_file (sa, filename, &offset) == -1)
        return -1;

      if (*size < offset)
        *size = offset;

      break;
    }

    case ' ': case '\t': case '\n': /* Skip whitespace. */
    case '\f': case '\r': case '\v':
      break;

    default:
    parse_error:
      nbdkit_error ("data parameter: parsing error at offset %zu", i);
      return -1;
    } /* switch */
  } /* for */

  return 0;
}
