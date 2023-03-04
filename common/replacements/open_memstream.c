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

/* Replacement for open_memstream for platforms which lack this function. */

#include <config.h>

#include <stdio.h>

#include "open_memstream.h"

#ifndef HAVE_OPEN_MEMSTREAM

#ifdef WIN32

/* Replacement open_memstream for Win32. */

#include <windows.h>

#include <assert.h>

/* This is provided by common/utils which hasn't been compiled yet.
 * Programs using this replacement will need to link to
 * libutils.la. XXX
 */
#include "../utils/vector.h"
#include "../utils/nbdkit-string.h"

/* Map FILE* that we return to the user buffer. */
struct file_to_memstream {
  FILE *fp;
  char tmpname[MAX_PATH];
  char **ptr;
  size_t *size;
};
DEFINE_VECTOR_TYPE (file_vector, struct file_to_memstream);
static file_vector files = empty_vector;

FILE *
open_memstream (char **ptr, size_t *size)
{
  struct file_to_memstream f2m;
  char tmppath[MAX_PATH];
  DWORD ret;
  FILE *fp;

  ret = GetTempPath (MAX_PATH, tmppath);
  if (ret > MAX_PATH || ret == 0)
    return NULL;

  ret = GetTempFileName (tmppath, TEXT ("nbdkit"), 0, f2m.tmpname);
  if (!ret)
    return NULL;

  fp = fopen (f2m.tmpname, "w+");
  if (fp == NULL)
    return NULL;

  f2m.fp = fp;
  f2m.ptr = ptr;
  f2m.size = size;
  if (file_vector_append (&files, f2m) == -1) {
    fclose (fp);
    return NULL;
  }

  return fp;
}

int
close_memstream (FILE *fp)
{
  size_t i;
  int c, r;
  string content = empty_vector;
  struct file_to_memstream *f2m;

  for (i = 0; i < files.len; ++i) {
    if (files.ptr[i].fp == fp)
      break;
  }
  assert (i < files.len);
  f2m = &files.ptr[i];

  /* Read the file back into memory. */
  rewind (fp);
  while ((c = getc (fp)) != EOF) {
    if (string_append (&content, c) == -1) {
    append_failed:
      fclose (fp);
      unlink (f2m->tmpname);
      free (content.ptr);
      file_vector_remove (&files, i);
      return -1;
    }
  }
  /* Make sure the buffer is \0-terminated but don't include this
   * in the buffer size returned below.
   */
  if (string_append (&content, 0) == -1) goto append_failed;

  r = fclose (fp);
  unlink (f2m->tmpname);
  if (r == EOF) {
    free (content.ptr);
    file_vector_remove (&files, i);
    return -1;
  }

  /* Pass the buffer to the user.  User will free it. */
  *(files.ptr[i].ptr) = content.ptr;
  *(files.ptr[i].size) = content.len - 1;
  file_vector_remove (&files, i);
  return 0;
}

#else /* !WIN32 */
#error "no replacement open_memstream is available on this platform"
#endif

#endif /* !HAVE_OPEN_MEMSTREAM */
