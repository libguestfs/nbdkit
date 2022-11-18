/* nbdkit
 * Copyright (C) 2018-2022 Red Hat Inc.
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
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define NBDKIT_API_VERSION 2

#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "ascii-string.h"

#include "call.h"
#include "methods.h"

void
sh_dump_plugin (void)
{
  const char *method = "dump_plugin";
  const char *script = get_script (method);
  const char *args[] = { script, method, NULL };
  CLEANUP_FREE_STRING string o = empty_vector;

  /* Dump information about the sh/eval features */
  printf ("max_known_status=%d\n", RET_FALSE);

  /* Dump any additional information from the script */
  if (script) {
    /* Call dump_plugin method. */
    switch (call_read (&o, args)) {
    case OK:
      printf ("%s", o.ptr);
      break;

    case MISSING:
      /* Ignore if the method was missing. */
      break;

    case ERROR:
      break;

    case RET_FALSE:
      nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                    script, method);
      errno = EIO;
      return;

    default: abort ();
    }
  }
}

int
sh_thread_model (void)
{
  const char *method = "thread_model";
  const char *script = get_script (method);
  const char *args[] = { script, method, NULL };
  CLEANUP_FREE_STRING string s = empty_vector;
  int r;

  /* For historical compatibility: the lack of a script is assumed to
   * be parallel, but an existing script with missing or unparseable
   * thread_model remains at the older (but safe)
   * serialize_all_requests.
   */
  if (!script)
    return NBDKIT_THREAD_MODEL_PARALLEL;

  switch (call_read (&s, args)) {
  case OK:
    if (s.len > 0 && s.ptr[s.len-1] == '\n')
      s.ptr[s.len-1] = '\0';
    if (ascii_strcasecmp (s.ptr, "parallel") == 0)
      r = NBDKIT_THREAD_MODEL_PARALLEL;
    else if (ascii_strcasecmp (s.ptr, "serialize_requests") == 0 ||
             ascii_strcasecmp (s.ptr, "serialize-requests") == 0)
      r = NBDKIT_THREAD_MODEL_SERIALIZE_REQUESTS;
    else if (ascii_strcasecmp (s.ptr, "serialize_all_requests") == 0 ||
             ascii_strcasecmp (s.ptr, "serialize-all-requests") == 0)
      r = NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS;
    else if (ascii_strcasecmp (s.ptr, "serialize_connections") == 0 ||
             ascii_strcasecmp (s.ptr, "serialize-connections") == 0)
      r = NBDKIT_THREAD_MODEL_SERIALIZE_CONNECTIONS;
    else {
      nbdkit_debug ("%s: ignoring unrecognized thread model: %s",
                    script, s.ptr);
      r = NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS;
    }
    return r;

  case MISSING:
    return NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS;

  case ERROR:
    return -1;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return -1;

  default: abort ();
  }
}

int
sh_get_ready (void)
{
  const char *method = "get_ready";
  const char *script = get_script (method);
  const char *args[] = { script, method, NULL };

  switch (call (args)) {
  case OK:
  case MISSING:
    return 0;

  case ERROR:
    return -1;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return -1;

  default: abort ();
  }
}

int
sh_after_fork (void)
{
  const char *method = "after_fork";
  const char *script = get_script (method);
  const char *args[] = { script, method, NULL };

  switch (call (args)) {
  case OK:
  case MISSING:
    return 0;

  case ERROR:
    return -1;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return -1;

  default: abort ();
  }
}

int
sh_preconnect (int readonly)
{
  const char *method = "preconnect";
  const char *script = get_script (method);
  const char *args[] =
    { script, method,
      readonly ? "true" : "false",
      NULL };

  switch (call (args)) {
  case OK:
  case MISSING:
    return 0;

  case ERROR:
    return -1;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return -1;

  default: abort ();
  }
}

struct sh_handle {
  string h;
  int can_flush;
  int can_zero;
};

/* If @s begins with @prefix, return the next offset, else NULL */
static const char *
skip_prefix (const char *s, const char *prefix)
{
  size_t len = strlen (prefix);
  if (strncmp (s, prefix, len) == 0)
    return s + len;
  return NULL;
}

static int
parse_exports (const char *script,
               const char *s, size_t slen, struct nbdkit_exports *exports)
{
  const char *n, *d, *p, *q;

  /* The first line determines how to parse the rest of s.  Keep
   * sh_default_export in sync with this.
   */
  if ((p = skip_prefix (s, "INTERLEAVED\n")) != NULL) {
    n = p;
    while ((d = strchr (n, '\n')) != NULL) {
      p = strchr (d + 1, '\n') ?: d + 1;
      CLEANUP_FREE char *name = strndup (n, d - n);
      CLEANUP_FREE char *desc = strndup (d + 1, p - d - 1);
      if (!name || !desc) {
        nbdkit_error ("%s: strndup: %m", script);
        return -1;
      }
      if (nbdkit_add_export (exports, name, desc) == -1)
        return -1;
      n = p + !!*p;
    }
  }
  else if ((p = skip_prefix (s, "NAMES+DESCRIPTIONS\n")) != NULL) {
    n = d = p;
    /* Searching from both ends, using memrchr, would be less work, but
     * memrchr is not widely portable. Multiple passes isn't too bad.
     */
    while (p && (p = strchr (p, '\n')) != NULL) {
      p = strchr (p + 1, '\n');
      if (p)
        p++;
      d = strchr (d, '\n') + 1;
    }
    s = d;
    while (n < s) {
      p = strchr (n, '\n');
      q = strchr (d, '\n') ?: d;
      CLEANUP_FREE char *name = strndup (n, p - n);
      CLEANUP_FREE char *desc = strndup (d, q - d);
      if (!name || !desc) {
        nbdkit_error ("%s: strndup: %m", script);
        return -1;
      }
      if (nbdkit_add_export (exports, name, desc) == -1)
        return -1;
      n = p + 1;
      d = q + 1;
    }
  }
  else {
    n = skip_prefix (s, "NAMES\n") ?: s;
    while ((p = strchr (n, '\n')) != NULL) {
      CLEANUP_FREE char *name = strndup (n, p - n);
      if (!name) {
        nbdkit_error ("%s: strndup: %m", script);
        return -1;
      }
      if (nbdkit_add_export (exports, name, NULL) == -1)
        return -1;
      n = p + 1;
    }
  }
  return 0;
}

int
sh_list_exports (int readonly, int is_tls, struct nbdkit_exports *exports)
{
  const char *method = "list_exports";
  const char *script = get_script (method);
  const char *args[] = { script, method, readonly ? "true" : "false",
                         is_tls ? "true" : "false", NULL };
  CLEANUP_FREE_STRING string s = empty_vector;

  switch (call_read (&s, args)) {
  case OK:
    return parse_exports (script, s.ptr, s.len, exports);

  case MISSING:
    return nbdkit_use_default_export (exports);

  case ERROR:
    return -1;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return -1;

  default: abort ();
  }
}

const char *
sh_default_export (int readonly, int is_tls)
{
  const char *method = "default_export";
  const char *script = get_script (method);
  const char *args[] = { script, method, readonly ? "true" : "false",
                         is_tls ? "true" : "false", NULL };
  CLEANUP_FREE_STRING string s = empty_vector;
  const char *p, *n;

  switch (call_read (&s, args)) {
  case OK:
    /* The first line determines how to parse the rest of s.  For now,
     * all export modes treat the next line as the first export.
     */
    if ((p = skip_prefix (s.ptr, "INTERLEAVED\n")) != NULL ||
        (p = skip_prefix (s.ptr, "NAMES+DESCRIPTIONS\n")) != NULL ||
        (p = skip_prefix (s.ptr, "NAMES\n")) != NULL)
      ;
    else
      p = s.ptr;
    n = strchr (p, '\n') ?: s.ptr + s.len;
    return nbdkit_strndup_intern (p, n - p);

  case MISSING:
    return "";

  case ERROR:
    return NULL;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return NULL;

  default: abort ();
  }
}

void *
sh_open (int readonly)
{
  const char *method = "open";
  const char *script = get_script (method);
  const char *args[] =
    { script, method,
      readonly ? "true" : "false",
      nbdkit_export_name () ? : "",
      nbdkit_is_tls () > 0 ? "true" : "false",
      NULL };
  struct sh_handle *h = calloc (1, sizeof *h);

  if (!h) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }
  h->can_flush = -1;
  h->can_zero = -1;

  /* We store the string returned by open in the handle. */
  switch (call_read (&h->h, args)) {
  case OK:
    /* Remove final newline if present. */
    if (h->h.len > 0 && h->h.ptr[h->h.len-1] == '\n')
      h->h.ptr[--h->h.len] = '\0';
    if (h->h.len > 0)
      nbdkit_debug ("sh: handle: %s", h->h.ptr);
    return h;

  case MISSING:
    /* Unlike regular C plugins, open is not required.  If it is
     * missing then we return "" as the handle.  Allocate a new string
     * for it because we don't know what call_read returned here.
     */
    string_reset (&h->h);
    if (string_reserve (&h->h, 1) == -1) {
      nbdkit_error ("realloc: %m");
      free (h);
      return NULL;
    }
    h->h.ptr[0] = '\0';
    return h;

  case ERROR:
    string_reset (&h->h);
    free (h);
    return NULL;

  case RET_FALSE:
    string_reset (&h->h);
    free (h);
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return NULL;

  default: abort ();
  }
}

void
sh_close (void *handle)
{
  const char *method = "close";
  const char *script = get_script (method);
  struct sh_handle *h = handle;
  const char *args[] = { script, method, h->h.ptr, NULL };

  switch (call (args)) {
  case OK:
  case MISSING:
  case ERROR:
  case RET_FALSE:
    string_reset (&h->h);
    free (h);
    return;
  default: abort ();
  }
}

const char *
sh_export_description (void *handle)
{
  const char *method = "export_description";
  const char *script = get_script (method);
  struct sh_handle *h = handle;
  const char *args[] = { script, method, h->h.ptr, NULL };
  CLEANUP_FREE_STRING string s = empty_vector;

  switch (call_read (&s, args)) {
  case OK:
    if (s.len > 0 && s.ptr[s.len-1] == '\n')
      s.ptr[s.len-1] = '\0';
    return nbdkit_strdup_intern (s.ptr);

  case MISSING:
    return NULL;

  case ERROR:
    return NULL;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return NULL;

  default: abort ();
  }
}

int64_t
sh_get_size (void *handle)
{
  const char *method = "get_size";
  const char *script = get_script (method);
  struct sh_handle *h = handle;
  const char *args[] = { script, method, h->h.ptr, NULL };
  CLEANUP_FREE_STRING string s = empty_vector;
  int64_t r;

  switch (call_read (&s, args)) {
  case OK:
    if (s.len > 0 && s.ptr[s.len-1] == '\n')
      s.ptr[s.len-1] = '\0';
    r = nbdkit_parse_size (s.ptr);
    if (r == -1)
      nbdkit_error ("%s: could not parse output from get_size method: %s",
                    script, s.ptr);
    return r;

  case MISSING:
    nbdkit_error ("%s: the get_size method is required", script);
    return -1;

  case ERROR:
    return -1;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return -1;

  default: abort ();
  }
}

int
sh_block_size (void *handle,
               uint32_t *minimum, uint32_t *preferred, uint32_t *maximum)
{
  const char *method = "block_size";
  const char *script = get_script (method);
  struct sh_handle *h = handle;
  const char *args[] = { script, method, h->h.ptr, NULL };
  CLEANUP_FREE_STRING string s = empty_vector;
  const char *delim = " \t\n";
  char *sp, *p;
  int64_t r;

  switch (call_read (&s, args)) {
  case OK:
    if ((p = strtok_r (s.ptr, delim, &sp)) == NULL) {
    parse_error:
      nbdkit_error ("%s: %s method cannot be parsed", script, method);
      return -1;
    }
    r = nbdkit_parse_size (p);
    if (r == -1 || r > UINT32_MAX)
      goto parse_error;
    *minimum = r;

    if ((p = strtok_r (NULL, delim, &sp)) == NULL)
      goto parse_error;
    r = nbdkit_parse_size (p);
    if (r == -1 || r > UINT32_MAX)
      goto parse_error;
    *preferred = r;

    if ((p = strtok_r (NULL, delim, &sp)) == NULL)
      goto parse_error;
    r = nbdkit_parse_size (p);
    if (r == -1 || r > UINT32_MAX)
      goto parse_error;
    *maximum = r;

#if 0
    nbdkit_debug ("setting block_size: "
                  "minimum=%" PRIu32 " "
                  "preferred=%" PRIu32 " "
                  "maximum=%" PRIu32,
                  *minimum, *preferred, *maximum);
#endif
    return 0;

  case MISSING:
    *minimum = *preferred = *maximum = 0;
    return 0;

  case ERROR:
    return -1;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return -1;

  default: abort ();
  }
}

int
sh_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
          uint32_t flags)
{
  const char *method = "pread";
  const char *script = get_script (method);
  struct sh_handle *h = handle;
  char cbuf[32], obuf[32];
  const char *args[] = { script, method, h->h.ptr, cbuf, obuf, NULL };
  CLEANUP_FREE_STRING string data = empty_vector;

  snprintf (cbuf, sizeof cbuf, "%" PRIu32, count);
  snprintf (obuf, sizeof obuf, "%" PRIu64, offset);

  switch (call_read (&data, args)) {
  case OK:
    if (count != data.len) {
      nbdkit_error ("%s: incorrect amount of data read: "
                    "expecting %" PRIu32 " bytes but "
                    "received %zu bytes from the script",
                    script, count, data.len);
      return -1;
    }
    memcpy (buf, data.ptr, count);
    return 0;

  case MISSING:
    nbdkit_error ("%s: the pread method is required", script);
    return -1;

  case ERROR:
    return -1;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return -1;

  default: abort ();
  }
}

/* Convert NBDKIT_FLAG_* to flags string. */
static void flag_append (const char *str, bool *comma, char **buf, size_t *len);

static void
flags_string (uint32_t flags, char *buf, size_t len)
{
  bool comma = false;

  buf[0] = '\0';

  if (flags & NBDKIT_FLAG_FUA)
    flag_append ("fua", &comma, &buf, &len);

  if (flags & NBDKIT_FLAG_MAY_TRIM)
    flag_append ("may_trim", &comma, &buf, &len);

  if (flags & NBDKIT_FLAG_REQ_ONE)
    flag_append ("req_one", &comma, &buf, &len);

  if (flags & NBDKIT_FLAG_FAST_ZERO)
    flag_append ("fast", &comma, &buf, &len);
}

static void
flag_append (const char *str, bool *comma, char **buf, size_t *len)
{
  size_t slen = strlen (str);

  if (*comma) {
    /* Too short flags buffer is an internal error so abort. */
    if (*len <= 1) abort ();
    strcpy (*buf, ",");
    (*buf)++;
    (*len)--;
  }

  if (*len <= slen) abort ();
  strcpy (*buf, str);
  (*buf) += slen;
  (*len) -= slen;

  *comma = true;
}

int
sh_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
           uint32_t flags)
{
  const char *method = "pwrite";
  const char *script = get_script (method);
  struct sh_handle *h = handle;
  char cbuf[32], obuf[32], fbuf[32];
  const char *args[] = { script, method, h->h.ptr, cbuf, obuf, fbuf, NULL };

  snprintf (cbuf, sizeof cbuf, "%" PRIu32, count);
  snprintf (obuf, sizeof obuf, "%" PRIu64, offset);
  flags_string (flags, fbuf, sizeof fbuf);

  switch (call_write (buf, count, args)) {
  case OK:
    return 0;

  case MISSING:
    nbdkit_error ("pwrite not implemented");
    return -1;

  case ERROR:
    return -1;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return -1;

  default: abort ();
  }
}

/* Common code for handling all boolean methods like can_write etc. */
static int
boolean_method (const char *script, const char *method,
                void *handle, int def)
{
  struct sh_handle *h = handle;
  const char *args[] = { script, method, h->h.ptr, NULL };

  switch (call (args)) {
  case OK:                      /* true */
    return 1;
  case RET_FALSE:               /* false */
    return 0;
  case MISSING:                 /* missing => caller chooses default */
    return def;
  case ERROR:                   /* error cases */
    return -1;
  default: abort ();
  }
}

int
sh_can_write (void *handle)
{
  const char *method = "can_write";
  const char *script = get_script (method);
  return boolean_method (script, method, handle, 0);
}

int
sh_can_flush (void *handle)
{
  const char *method = "can_flush";
  const char *script;
  struct sh_handle *h = handle;

  if (h->can_flush >= 0)
    return h->can_flush;

  script = get_script (method);
  return h->can_flush = boolean_method (script, method, handle, 0);
}

int
sh_is_rotational (void *handle)
{
  const char *method = "is_rotational";
  const char *script = get_script (method);
  return boolean_method (script, method, handle, 0);
}

int
sh_can_trim (void *handle)
{
  const char *method = "can_trim";
  const char *script = get_script (method);
  return boolean_method (script, method, handle, 0);
}

int
sh_can_zero (void *handle)
{
  const char *method = "can_zero";
  const char *script;
  struct sh_handle *h = handle;

  if (h->can_zero >= 0)
    return h->can_zero;

  script = get_script (method);
  return h->can_zero = boolean_method (script, method, handle, 0);
}

int
sh_can_extents (void *handle)
{
  const char *method = "can_extents";
  const char *script = get_script (method);
  return boolean_method (script, method, handle, 0);
}

int
sh_can_multi_conn (void *handle)
{
  const char *method = "can_multi_conn";
  const char *script = get_script (method);
  return boolean_method (script, method, handle, 0);
}

/* Not a boolean method, the method prints "none", "emulate" or "native". */
int
sh_can_fua (void *handle)
{
  const char *method = "can_fua";
  const char *script = get_script (method);
  struct sh_handle *h = handle;
  const char *args[] = { script, method, h->h.ptr, NULL };
  CLEANUP_FREE_STRING string s = empty_vector;
  int r;

  switch (call_read (&s, args)) {
  case OK:
    if (s.len > 0 && s.ptr[s.len-1] == '\n')
      s.ptr[s.len-1] = '\0';
    if (ascii_strcasecmp (s.ptr, "none") == 0)
      r = NBDKIT_FUA_NONE;
    else if (ascii_strcasecmp (s.ptr, "emulate") == 0)
      r = NBDKIT_FUA_EMULATE;
    else if (ascii_strcasecmp (s.ptr, "native") == 0)
      r = NBDKIT_FUA_NATIVE;
    else {
      nbdkit_error ("%s: could not parse output from %s method: %s",
                    script, method, s.ptr);
      r = -1;
    }
    return r;

  case MISSING:
    /* Check if the plugin claims to support flush. */
    switch (sh_can_flush (handle)) {
    case -1:
      return -1;
    case 0:
      return NBDKIT_FUA_NONE;
    case 1:
      return NBDKIT_FUA_EMULATE;
    default:
      abort ();
    }

  case ERROR:
    return -1;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return -1;

  default: abort ();
  }
}

/* Not a boolean method, the method prints "none", "emulate" or "native". */
int
sh_can_cache (void *handle)
{
  const char *method = "can_cache";
  const char *script = get_script (method);
  struct sh_handle *h = handle;
  const char *args[] = { script, method, h->h.ptr, NULL };
  CLEANUP_FREE_STRING string s = empty_vector;
  int r;

  switch (call_read (&s, args)) {
  case OK:
    if (s.len > 0 && s.ptr[s.len-1] == '\n')
      s.ptr[s.len-1] = '\0';
    if (ascii_strcasecmp (s.ptr, "none") == 0)
      r = NBDKIT_CACHE_NONE;
    else if (ascii_strcasecmp (s.ptr, "emulate") == 0)
      r = NBDKIT_CACHE_EMULATE;
    else if (ascii_strcasecmp (s.ptr, "native") == 0)
      r = NBDKIT_CACHE_NATIVE;
    else {
      nbdkit_error ("%s: could not parse output from %s method: %s",
                    script, method, s.ptr);
      r = -1;
    }
    return r;

  case MISSING:
    /* NBDKIT_CACHE_EMULATE means that nbdkit will call .pread.  However
     * we cannot know if that fallback would be efficient, so the safest
     * default is to return NBDKIT_CACHE_NONE.
     */
    return NBDKIT_CACHE_NONE;

  case ERROR:
    return -1;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return -1;

  default: abort ();
  }
}

int
sh_can_fast_zero (void *handle)
{
  const char *method = "can_fast_zero";
  const char *script = get_script (method);
  int r = boolean_method (script, method, handle, 2);
  if (r < 2)
    return r;
  /* We need to duplicate the logic of plugins.c: if can_fast_zero is
   * missing, we advertise fast fail support when can_zero is false.
   */
  r = sh_can_zero (handle);
  if (r == -1)
    return -1;
  return !r;
}

int
sh_flush (void *handle, uint32_t flags)
{
  const char *method = "flush";
  const char *script = get_script (method);
  struct sh_handle *h = handle;
  const char *args[] = { script, method, h->h.ptr, NULL };

  switch (call (args)) {
  case OK:
    return 0;

  case MISSING:
    /* Ignore lack of flush callback. */
    return 0;

  case ERROR:                   /* error cases */
    return -1;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return -1;

  default: abort ();
  }
}

int
sh_trim (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  const char *method = "trim";
  const char *script = get_script (method);
  struct sh_handle *h = handle;
  char cbuf[32], obuf[32], fbuf[32];
  const char *args[] = { script, method, h->h.ptr, cbuf, obuf, fbuf, NULL };

  snprintf (cbuf, sizeof cbuf, "%" PRIu32, count);
  snprintf (obuf, sizeof obuf, "%" PRIu64, offset);
  flags_string (flags, fbuf, sizeof fbuf);

  switch (call (args)) {
  case OK:
    return 0;

  case MISSING:
    /* Ignore lack of trim callback. */
    return 0;

  case ERROR:
    return -1;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return -1;

  default: abort ();
  }
}

int
sh_zero (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  const char *method = "zero";
  const char *script = get_script (method);
  struct sh_handle *h = handle;
  char cbuf[32], obuf[32], fbuf[32];
  const char *args[] = { script, method, h->h.ptr, cbuf, obuf, fbuf, NULL };

  snprintf (cbuf, sizeof cbuf, "%" PRIu32, count);
  snprintf (obuf, sizeof obuf, "%" PRIu64, offset);
  flags_string (flags, fbuf, sizeof fbuf);

  switch (call (args)) {
  case OK:
    return 0;

  case MISSING:
    nbdkit_debug ("zero falling back to pwrite");
    errno = EOPNOTSUPP;
    return -1;

  case ERROR:
    return -1;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return -1;

  default: abort ();
  }
}

static int
parse_extents (const char *script,
               const char *s, size_t slen, struct nbdkit_extents *extents)
{
  FILE *fp = NULL;
  CLEANUP_FREE char *line = NULL;
  size_t linelen = 0;
  ssize_t len;
  int ret = -1;

  fp = fmemopen ((void *) s, slen, "r");
  if (!fp) {
    nbdkit_error ("%s: extents: fmemopen: %m", script);
    goto out;
  }

  while ((len = getline (&line, &linelen, fp)) != -1) {
    const char *delim = " \t";
    char *sp, *p;
    int64_t offset, length;
    uint32_t type;

    if (len > 0 && line[len-1] == '\n') {
      line[len-1] = '\0';
      len--;
    }

    if ((p = strtok_r (line, delim, &sp)) == NULL) {
    parse_error:
      nbdkit_error ("%s: extents: cannot parse %s", script, line);
      goto out;
    }
    offset = nbdkit_parse_size (p);
    if (offset == -1)
      goto out;

    if ((p = strtok_r (NULL, delim, &sp)) == NULL)
      goto parse_error;
    length = nbdkit_parse_size (p);
    if (length == -1)
      goto out;

    if ((p = strtok_r (NULL, delim, &sp)) == NULL)
      /* empty type field means allocated data (0) */
      type = 0;
    else if (sscanf (p, "%" SCNu32, &type) == 1)
      ;
    else {
      type = 0;
      if (strstr (p, "hole") != NULL)
        type |= NBDKIT_EXTENT_HOLE;
      if (strstr (p, "zero") != NULL)
        type |= NBDKIT_EXTENT_ZERO;
    }

    nbdkit_debug ("%s: adding extent %" PRIi64 " %" PRIi64 " %" PRIu32,
                  script, offset, length, type);
    if (nbdkit_add_extent (extents, offset, length, type) == -1)
      goto out;
  }

  ret = 0;

 out:
  if (fp)
    fclose (fp);
  return ret;
}

int
sh_extents (void *handle, uint32_t count, uint64_t offset, uint32_t flags,
            struct nbdkit_extents *extents)
{
  const char *method = "extents";
  const char *script = get_script (method);
  struct sh_handle *h = handle;
  char cbuf[32], obuf[32], fbuf[32];
  const char *args[] = { script, method, h->h.ptr, cbuf, obuf, fbuf, NULL };
  CLEANUP_FREE_STRING string s = empty_vector;
  int r;

  snprintf (cbuf, sizeof cbuf, "%" PRIu32, count);
  snprintf (obuf, sizeof obuf, "%" PRIu64, offset);
  flags_string (flags, fbuf, sizeof fbuf);

  switch (call_read (&s, args)) {
  case OK:
    r = parse_extents (script, s.ptr, s.len, extents);
    return r;

  case MISSING:
    /* extents method should not have been called unless the script
     * defined a can_extents method which returns true, so if this
     * happens it's a script error.
     */
    nbdkit_error ("%s: can_extents returned true, "
                  "but extents method is not defined",
                  script);
    errno = EIO;
    return -1;

  case ERROR:
    return -1;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return -1;

  default: abort ();
  }
}

int
sh_cache (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  const char *method = "cache";
  const char *script = get_script (method);
  struct sh_handle *h = handle;
  char cbuf[32], obuf[32];
  const char *args[] = { script, method, h->h.ptr, cbuf, obuf, NULL };

  snprintf (cbuf, sizeof cbuf, "%" PRIu32, count);
  snprintf (obuf, sizeof obuf, "%" PRIu64, offset);
  assert (!flags);

  switch (call (args)) {
  case OK:
    return 0;

  case MISSING:
    /* Ignore lack of cache callback. */
    return 0;

  case ERROR:
    return -1;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, method);
    errno = EIO;
    return -1;

  default: abort ();
  }
}
