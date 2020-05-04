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
#include <string.h>
#include <unistd.h>

/* Print str to fp, shell quoting if necessary.  This comes from
 * libguestfs, but was written by me so I'm relicensing it to a BSD
 * license for nbdkit.
 */
void
shell_quote (const char *str, FILE *fp)
{
  /* Note possible bug in this list (XXX):
   * https://www.redhat.com/archives/libguestfs/2019-February/msg00036.html
   */
  const char *safe_chars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_=,:/";
  size_t i, len;

  /* If the string consists only of safe characters, output it as-is. */
  len = strlen (str);
  if (len > 0 && len == strspn (str, safe_chars)) {
    fputs (str, fp);
    return;
  }

  /* Double-quote the string. */
  fputc ('"', fp);
  for (i = 0; i < len; ++i) {
    switch (str[i]) {
    case '$': case '`': case '\\': case '"':
      fputc ('\\', fp);
      /*FALLTHROUGH*/
    default:
      fputc (str[i], fp);
    }
  }
  fputc ('"', fp);
}

/* Print str to fp, URI quoting if necessary.
 * The resulting string is safe for use in a URI path or query component,
 * and can be passed through the shell without further quoting.
 */
void
uri_quote (const char *str, FILE *fp)
{
  /* safe_chars contains the RFC 3986 unreserved characters plus '/'. */
  const char *safe_chars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_~/";
  size_t i, len;

  /* If the string consists only of safe characters, output it as-is. */
  len = strlen (str);
  if (len == strspn (str, safe_chars)) {
    fputs (str, fp);
    return;
  }

  for (i = 0; i < len; ++i) {
    if (strchr (safe_chars, str[i]))
      fputc (str[i], fp);
    else
      fprintf (fp, "%%%02X", str[i] & 0xff);
  }
}
