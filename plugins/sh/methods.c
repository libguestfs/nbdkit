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
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define NBDKIT_API_VERSION 2

#include <nbdkit-plugin.h>

#include "cleanup.h"

#include "call.h"
#include "methods.h"

void
sh_dump_plugin (void)
{
  const char *method = "dump_plugin";
  const char *script = get_script (method);
  const char *args[] = { script, method, NULL };
  CLEANUP_FREE char *o = NULL;
  size_t olen;

  if (script) {
    /* Call dump_plugin method. */
    switch (call_read (&o, &olen, args)) {
    case OK:
      printf ("%s", o);
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
  CLEANUP_FREE char *s = NULL;
  size_t slen;
  int r;

  /* For historical compatibility: the lack of a script is assumed to
   * be parallel, but an existing script with missing or unparseable
   * thread_model remains at the older (but safe)
   * serialize_all_requests.
   */
  if (!script)
    return NBDKIT_THREAD_MODEL_PARALLEL;

  switch (call_read (&s, &slen, args)) {
  case OK:
    if (slen > 0 && s[slen-1] == '\n')
      s[slen-1] = '\0';
    if (strcasecmp (s, "parallel") == 0)
      r = NBDKIT_THREAD_MODEL_PARALLEL;
    else if (strcasecmp (s, "serialize_requests") == 0 ||
             strcasecmp (s, "serialize-requests") == 0)
      r = NBDKIT_THREAD_MODEL_SERIALIZE_REQUESTS;
    else if (strcasecmp (s, "serialize_all_requests") == 0 ||
             strcasecmp (s, "serialize-all-requests") == 0)
      r = NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS;
    else if (strcasecmp (s, "serialize_connections") == 0 ||
             strcasecmp (s, "serialize-connections") == 0)
      r = NBDKIT_THREAD_MODEL_SERIALIZE_CONNECTIONS;
    else {
      nbdkit_debug ("%s: ignoring unrecognized thread model: %s",
                    script, s);
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
sh_preconnect (int readonly)
{
  const char *method = "preconnect";
  const char *script = get_script (method);
  const char *args[] =
    { script, method,
      readonly ? "true" : "false",
      nbdkit_export_name () ? : "",
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
  char *h;
  int can_flush;
  int can_zero;
};

void *
sh_open (int readonly)
{
  const char *method = "open";
  const char *script = get_script (method);
  size_t hlen;
  const char *args[] =
    { script, method,
      readonly ? "true" : "false",
      nbdkit_export_name () ? : "",
      NULL };
  struct sh_handle *h = malloc (sizeof *h);

  if (!h) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }
  h->can_flush = -1;
  h->can_zero = -1;

  /* We store the string returned by open in the handle. */
  switch (call_read (&h->h, &hlen, args)) {
  case OK:
    /* Remove final newline if present. */
    if (hlen > 0 && h->h[hlen-1] == '\n') {
      h->h[hlen-1] = '\0';
      hlen--;
    }
    if (hlen > 0)
      nbdkit_debug ("sh: handle: %s", h->h);
    return h;

  case MISSING:
    /* Unlike regular C plugins, open is not required.  If it is
     * missing then we return "" as the handle.  Allocate a new string
     * for it because we don't know what call_read returned here.
     */
    free (h->h);
    h->h = strdup ("");
    if (h->h == NULL)
      nbdkit_error ("strdup: %m");
    return h;

  case ERROR:
    free (h->h);
    free (h);
    return NULL;

  case RET_FALSE:
    free (h->h);
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
  const char *args[] = { script, method, h->h, NULL };

  switch (call (args)) {
  case OK:
  case MISSING:
  case ERROR:
  case RET_FALSE:
    free (h->h);
    free (h);
    return;
  default: abort ();
  }
}

int64_t
sh_get_size (void *handle)
{
  const char *method = "get_size";
  const char *script = get_script (method);
  struct sh_handle *h = handle;
  const char *args[] = { script, method, h->h, NULL };
  CLEANUP_FREE char *s = NULL;
  size_t slen;
  int64_t r;

  switch (call_read (&s, &slen, args)) {
  case OK:
    if (slen > 0 && s[slen-1] == '\n')
      s[slen-1] = '\0';
    r = nbdkit_parse_size (s);
    if (r == -1)
      nbdkit_error ("%s: could not parse output from get_size method: %s",
                    script, s);
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
sh_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
          uint32_t flags)
{
  const char *method = "pread";
  const char *script = get_script (method);
  struct sh_handle *h = handle;
  char cbuf[32], obuf[32];
  const char *args[] = { script, method, h->h, cbuf, obuf, NULL };
  CLEANUP_FREE char *data = NULL;
  size_t len;

  snprintf (cbuf, sizeof cbuf, "%" PRIu32, count);
  snprintf (obuf, sizeof obuf, "%" PRIu64, offset);

  switch (call_read (&data, &len, args)) {
  case OK:
    if (count != len) {
      nbdkit_error ("%s: incorrect amount of data read: "
                    "expecting %" PRIu32 " bytes but "
                    "received %zu bytes from the script",
                    script, count, len);
      return -1;
    }
    memcpy (buf, data, count);
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
  const char *args[] = { script, method, h->h, cbuf, obuf, fbuf, NULL };

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
  const char *args[] = { script, method, h->h, NULL };

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
  const char *args[] = { script, method, h->h, NULL };
  CLEANUP_FREE char *s = NULL;
  size_t slen;
  int r;

  switch (call_read (&s, &slen, args)) {
  case OK:
    if (slen > 0 && s[slen-1] == '\n')
      s[slen-1] = '\0';
    if (strcasecmp (s, "none") == 0)
      r = NBDKIT_FUA_NONE;
    else if (strcasecmp (s, "emulate") == 0)
      r = NBDKIT_FUA_EMULATE;
    else if (strcasecmp (s, "native") == 0)
      r = NBDKIT_FUA_NATIVE;
    else {
      nbdkit_error ("%s: could not parse output from %s method: %s",
                    script, method, s);
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
  const char *args[] = { script, method, h->h, NULL };
  CLEANUP_FREE char *s = NULL;
  size_t slen;
  int r;

  switch (call_read (&s, &slen, args)) {
  case OK:
    if (slen > 0 && s[slen-1] == '\n')
      s[slen-1] = '\0';
    if (strcasecmp (s, "none") == 0)
      r = NBDKIT_CACHE_NONE;
    else if (strcasecmp (s, "emulate") == 0)
      r = NBDKIT_CACHE_EMULATE;
    else if (strcasecmp (s, "native") == 0)
      r = NBDKIT_CACHE_NATIVE;
    else {
      nbdkit_error ("%s: could not parse output from %s method: %s",
                    script, method, s);
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
  const char *args[] = { script, method, h->h, NULL };

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
  const char *args[] = { script, method, h->h, cbuf, obuf, fbuf, NULL };

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
  const char *args[] = { script, method, h->h, cbuf, obuf, fbuf, NULL };

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
  const char *args[] = { script, method, h->h, cbuf, obuf, fbuf, NULL };
  CLEANUP_FREE char *s = NULL;
  size_t slen;
  int r;

  snprintf (cbuf, sizeof cbuf, "%" PRIu32, count);
  snprintf (obuf, sizeof obuf, "%" PRIu64, offset);
  flags_string (flags, fbuf, sizeof fbuf);

  switch (call_read (&s, &slen, args)) {
  case OK:
    r = parse_extents (script, s, slen, extents);
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
  const char *args[] = { script, method, h->h, cbuf, obuf, NULL };

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
