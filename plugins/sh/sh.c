/* nbdkit
 * Copyright (C) 2018-2019 Red Hat Inc.
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
#include <sys/stat.h>

#define NBDKIT_API_VERSION 2

#include <nbdkit-plugin.h>

#include "call.h"

char tmpdir[] = "/tmp/nbdkitshXXXXXX";
char *script;

static void
sh_load (void)
{
  /* Create the temporary directory for the shell script to use. */
  if (mkdtemp (tmpdir) == NULL) {
    nbdkit_error ("mkdtemp: /tmp: %m");
    exit (EXIT_FAILURE);
  }
  nbdkit_debug ("sh: load: tmpdir: %s", tmpdir);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
static void
sh_unload (void)
{
  const size_t tmpdir_len = strlen (tmpdir);
  char cmd[7 + tmpdir_len + 1]; /* "rm -rf " + tmpdir + \0 */

  /* Run the unload method.  Ignore all errors. */
  if (script) {
    const char *args[] = { script, "unload", NULL };

    call (args);
  }

  /* Delete the temporary directory.  Ignore all errors. */
  snprintf (cmd, sizeof cmd, "rm -rf %s", tmpdir);
  system (cmd);

  free (script);
}
#pragma GCC diagnostic pop

static void
sh_dump_plugin (void)
{
  const char *args[] = { script, "dump_plugin", NULL };
  char *o;
  size_t olen;

  if (script) {
    /* Call dump_plugin method. */
    switch (call_read (&o, &olen, args)) {
    case OK:
      printf ("%s", o);
      free (o);
      break;

    case MISSING:
      /* Ignore if the method was missing. */
      break;

    case ERROR:
      free (o);
      break;

    case RET_FALSE:
      nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                    script, "dump_plugin");
      errno = EIO;
      return;

    default: abort ();
    }
  }
}

/* This implements the "inline script" feature.  Read stdin into a
 * temporary file and return the name of the file which the caller
 * must free.  For convenience we put the temporary file into tmpdir
 * but that's an implementation detail.
 */
static char *
inline_script (void)
{
  const char scriptname[] = "inline-script.sh";
  char *filename = NULL;
  char *cmd = NULL;

  if (asprintf (&filename, "%s/%s", tmpdir, scriptname) == -1) {
    nbdkit_error ("asprintf: %m");
    goto err;
  }

  /* Safe because both the tmpdir and script name are controlled by us
   * and don't contain characters that need quoting.
   */
  if (asprintf (&cmd, "cat > %s", filename) == -1) {
    nbdkit_error ("asprintf: %m");
    goto err;
  }

  if (system (cmd) != 0) {
    nbdkit_error ("sh: failed to copy inline script to temporary file");
    goto err;
  }

  if (chmod (filename, 0500) == -1) {
    nbdkit_error ("chmod: %s: %m", filename);
    goto err;
  }

  free (cmd);
  return filename;

 err:
  free (filename);
  free (cmd);
  return NULL;
}

static int
sh_config (const char *key, const char *value)
{
  if (!script) {
    /* The first parameter MUST be "script". */
    if (strcmp (key, "script") != 0) {
      nbdkit_error ("the first parameter must be script=/path/to/script");
      return -1;
    }

    /* If the script name is not "-" then it's expected to be a
     * filename, otherwise it's an inline script which must be read
     * into a temporary file.  Either way we want an absolute path.
     */
    if (strcmp (value, "-") != 0)
      script = nbdkit_realpath (value);
    else
      script = inline_script ();
    if (script == NULL)
      return -1;

    /* Call the load method. */
    const char *args[] = { script, "load", NULL };
    switch (call (args)) {
    case OK:
    case MISSING:
      break;

    case ERROR:
      return -1;

    case RET_FALSE:
      nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                    script, "load");
      errno = EIO;
      return -1;

    default: abort ();
    }
  }
  else {
    const char *args[] = { script, "config", key, value, NULL };

    switch (call (args)) {
    case OK:
      return 0;

    case MISSING:
      /* Emulate what core nbdkit does if a config callback is NULL. */
      nbdkit_error ("%s: this plugin does not need command line configuration",
                    script);
      return -1;

    case ERROR:
      return -1;

    case RET_FALSE:
      nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                    script, "config");
      errno = EIO;
      return -1;

    default: abort ();
    }
  }

  return 0;
}

static int
sh_config_complete (void)
{
  const char *args[] = { script, "config_complete", NULL };

  if (!script) {
    nbdkit_error ("missing script parameter");
    return -1;
  }

  switch (call (args)) {
  case OK:
  case MISSING:
    return 0;

  case ERROR:
    return -1;

  case RET_FALSE:
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, "config_complete");
    errno = EIO;
    return -1;

  default: abort ();
  }
}

static void *
sh_open (int readonly)
{
  char *h = NULL;
  size_t hlen;
  const char *args[] = { script, "open", readonly ? "true" : "false", NULL };

  /* We store the string returned by open in the handle. */
  switch (call_read (&h, &hlen, args)) {
  case OK:
    /* Remove final newline if present. */
    if (hlen > 0 && h[hlen-1] == '\n') {
      h[hlen-1] = '\0';
      hlen--;
    }
    if (hlen > 0)
      nbdkit_debug ("sh: handle: %s", h);
    return h;

  case MISSING:
    /* Unlike regular C plugins, open is not required.  If it is
     * missing then we return "" as the handle.  Allocate a new string
     * for it because we don't know what call_read returned here.
     */
    free (h);
    h = strdup ("");
    if (h == NULL)
      nbdkit_error ("strdup: %m");
    return h;

  case ERROR:
    free (h);
    return NULL;

  case RET_FALSE:
    free (h);
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, "open");
    errno = EIO;
    return NULL;

  default: abort ();
  }
}

static void
sh_close (void *handle)
{
  char *h = handle;
  const char *args[] = { script, "close", h, NULL };

  switch (call (args)) {
  case OK:
  case MISSING:
  case ERROR:
  case RET_FALSE:
    free (h);
    return;
  default: abort ();
  }
}

static int64_t
sh_get_size (void *handle)
{
  char *h = handle;
  const char *args[] = { script, "get_size", h, NULL };
  char *s = NULL;
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
    free (s);
    return r;

  case MISSING:
    free (s);
    nbdkit_error ("%s: the get_size method is required", script);
    return -1;

  case ERROR:
    free (s);
    return -1;

  case RET_FALSE:
    free (s);
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, "get_size");
    errno = EIO;
    return -1;

  default: abort ();
  }
}

static int
sh_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
          uint32_t flags)
{
  char *h = handle;
  char cbuf[32], obuf[32];
  const char *args[] = { script, "pread", h, cbuf, obuf, NULL };
  char *data = NULL;
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
      free (data);
      return -1;
    }
    memcpy (buf, data, count);
    free (data);
    return 0;

  case MISSING:
    free (data);
    nbdkit_error ("%s: the pread method is required", script);
    return -1;

  case ERROR:
    free (data);
    return -1;

  case RET_FALSE:
    free (data);
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, "pread");
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

static int
sh_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
           uint32_t flags)
{
  char *h = handle;
  char cbuf[32], obuf[32], fbuf[32];
  const char *args[] = { script, "pwrite", h, cbuf, obuf, fbuf, NULL };

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
                  script, "pwrite");
    errno = EIO;
    return -1;

  default: abort ();
  }
}

/* Common code for handling all boolean methods like can_write etc. */
static int
boolean_method (void *handle, const char *method_name)
{
  char *h = handle;
  const char *args[] = { script, method_name, h, NULL };

  switch (call (args)) {
  case OK:                      /* true */
    return 1;
  case RET_FALSE:               /* false */
    return 0;
  case MISSING:                 /* missing => assume false */
    return 0;
  case ERROR:                   /* error cases */
    return -1;
  default: abort ();
  }
}

static int
sh_can_write (void *handle)
{
  return boolean_method (handle, "can_write");
}

static int
sh_can_flush (void *handle)
{
  return boolean_method (handle, "can_flush");
}

static int
sh_is_rotational (void *handle)
{
  return boolean_method (handle, "is_rotational");
}

static int
sh_can_trim (void *handle)
{
  return boolean_method (handle, "can_trim");
}

static int
sh_can_zero (void *handle)
{
  return boolean_method (handle, "can_zero");
}

static int
sh_can_extents (void *handle)
{
  return boolean_method (handle, "can_extents");
}

/* Not a boolean method, the method prints "none", "emulate" or "native". */
static int
sh_can_fua (void *handle)
{
  char *h = handle;
  const char *args[] = { script, "can_fua", h, NULL };
  char *s = NULL;
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
      nbdkit_error ("%s: could not parse output from can_fua method: %s",
                    script, s);
      r = -1;
    }
    free (s);
    return r;

  case MISSING:
    free (s);
    /* NBDKIT_FUA_EMULATE means that nbdkit will call .flush.  However
     * we cannot know if that callback exists, so the safest default
     * is to return NBDKIT_FUA_NONE.
     */
    return NBDKIT_FUA_NONE;

  case ERROR:
    free (s);
    return -1;

  case RET_FALSE:
    free (s);
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, "can_fua");
    errno = EIO;
    return -1;

  default: abort ();
  }
}

static int
sh_can_multi_conn (void *handle)
{
  return boolean_method (handle, "can_multi_conn");
}

static int
sh_flush (void *handle, uint32_t flags)
{
  char *h = handle;
  const char *args[] = { script, "flush", h, NULL };

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
                  script, "flush");
    errno = EIO;
    return -1;

  default: abort ();
  }
}

static int
sh_trim (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  char *h = handle;
  char cbuf[32], obuf[32], fbuf[32];
  const char *args[] = { script, "trim", h, cbuf, obuf, fbuf, NULL };

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
                  script, "trim");
    errno = EIO;
    return -1;

  default: abort ();
  }
}

static int
sh_zero (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  char *h = handle;
  char cbuf[32], obuf[32], fbuf[32];
  const char *args[] = { script, "zero", h, cbuf, obuf, fbuf, NULL };

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
                  script, "zero");
    errno = EIO;
    return -1;

  default: abort ();
  }
}

static int
parse_extents (const char *s, size_t slen, struct nbdkit_extents *extents)
{
  FILE *fp = NULL;
  char *line = NULL;
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
  free (line);
  if (fp)
    fclose (fp);
  return ret;
}

static int
sh_extents (void *handle, uint32_t count, uint64_t offset, uint32_t flags,
            struct nbdkit_extents *extents)
{
  char *h = handle;
  char cbuf[32], obuf[32], fbuf[32];
  const char *args[] = { script, "extents", h, cbuf, obuf, fbuf, NULL };
  char *s = NULL;
  size_t slen;
  int r;

  snprintf (cbuf, sizeof cbuf, "%" PRIu32, count);
  snprintf (obuf, sizeof obuf, "%" PRIu64, offset);
  flags_string (flags, fbuf, sizeof fbuf);

  switch (call_read (&s, &slen, args)) {
  case OK:
    r = parse_extents (s, slen, extents);
    free (s);
    return r;

  case MISSING:
    /* extents method should not have been called unless the script
     * defined a can_extents method which returns true, so if this
     * happens it's a script error.
     */
    free (s);
    nbdkit_error ("%s: can_extents returned true, "
                  "but extents method is not defined",
                  script);
    errno = EIO;
    return -1;

  case ERROR:
    free (s);
    return -1;

  case RET_FALSE:
    free (s);
    nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                  script, "can_fua");
    errno = EIO;
    return -1;

  default: abort ();
  }
}

#define sh_config_help \
  "script=<FILENAME>     (required) The shell script to run.\n" \
  "[other arguments may be used by the plugin that you load]"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

static struct nbdkit_plugin plugin = {
  .name              = "sh",
  .version           = PACKAGE_VERSION,
  .load              = sh_load,
  .unload            = sh_unload,

  .dump_plugin       = sh_dump_plugin,

  .config            = sh_config,
  .config_complete   = sh_config_complete,
  .config_help       = sh_config_help,

  .open              = sh_open,
  .close             = sh_close,

  .get_size          = sh_get_size,
  .can_write         = sh_can_write,
  .can_flush         = sh_can_flush,
  .is_rotational     = sh_is_rotational,
  .can_trim          = sh_can_trim,
  .can_zero          = sh_can_zero,
  .can_extents       = sh_can_extents,
  .can_fua           = sh_can_fua,
  .can_multi_conn    = sh_can_multi_conn,

  .pread             = sh_pread,
  .pwrite            = sh_pwrite,
  .flush             = sh_flush,
  .trim              = sh_trim,
  .zero              = sh_zero,
  .extents           = sh_extents,

  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
