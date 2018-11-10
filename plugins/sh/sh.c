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
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

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
    default:
      free (o);
    }
  }
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
    script = nbdkit_realpath (value);
    if (script == NULL)
      return -1;

    /* Call the load method. */
    const char *args[] = { script, "load", NULL };
    switch (call (args)) {
    case OK:
    case MISSING:
      break;
    case ERROR:
    default:
      return -1;
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
    default:
      return -1;
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
  default:
    return -1;
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
    return h;
  case MISSING:
    nbdkit_error ("%s: the open method is required", script);
    free (h);
    return NULL;
  case ERROR:
  default:
    free (h);
    return NULL;
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
    nbdkit_error ("%s: the get_size method is required", script);
    free (s);
    return -1;

  case ERROR:
  default:
    free (s);
    return -1;
  }
}

static int
sh_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
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
      nbdkit_error ("%s: incorrect amount of data read: expecting %" PRIu32 " bytes but received %zu bytes from the script",
                    script, count, len);
      free (data);
      return -1;
    }
    memcpy (buf, data, count);
    free (data);
    return 0;

  case MISSING:
    nbdkit_error ("%s: the pread method is required", script);
    free (data);
    return -1;

  case ERROR:
  default:
    free (data);
    return -1;
  }
}

static int
sh_pwrite (void *handle, const void *buf,
                   uint32_t count, uint64_t offset)
{
  char *h = handle;
  char cbuf[32], obuf[32];
  const char *args[] = { script, "pwrite", h, cbuf, obuf, NULL };

  snprintf (cbuf, sizeof cbuf, "%" PRIu32, count);
  snprintf (obuf, sizeof obuf, "%" PRIu64, offset);

  switch (call_write (buf, count, args)) {
  case OK:
    return 0;

  case MISSING:
    nbdkit_error ("pwrite not implemented");
    return -1;

  case ERROR:
  default:
    return -1;
  }
}

static int
sh_can_write (void *handle)
{
  char *h = handle;
  const char *args[] = { script, "can_write", h, NULL };

  switch (call (args)) {
  case OK:                      /* true */
    return 1;
  case RET_FALSE:               /* false */
    return 0;
  case MISSING:                 /* missing => assume false */
    return 0;
  case ERROR:                   /* error cases */
  default:
    return -1;
  }
}

static int
sh_can_flush (void *handle)
{
  char *h = handle;
  const char *args[] = { script, "can_flush", h, NULL };

  switch (call (args)) {
  case OK:                      /* true */
    return 1;
  case RET_FALSE:               /* false */
    return 0;
  case MISSING:                 /* missing => assume false */
    return 0;
  case ERROR:                   /* error cases */
  default:
    return -1;
  }
}

static int
sh_can_trim (void *handle)
{
  char *h = handle;
  const char *args[] = { script, "can_trim", h, NULL };

  switch (call (args)) {
  case OK:                      /* true */
    return 1;
  case RET_FALSE:               /* false */
    return 0;
  case MISSING:                 /* missing => assume false */
    return 0;
  case ERROR:                   /* error cases */
  default:
    return -1;
  }
}

static int
sh_is_rotational (void *handle)
{
  char *h = handle;
  const char *args[] = { script, "is_rotational", h, NULL };

  switch (call (args)) {
  case OK:                      /* true */
    return 1;
  case RET_FALSE:               /* false */
    return 0;
  case MISSING:                 /* missing => assume false */
    return 0;
  case ERROR:                   /* error cases */
  default:
    return -1;
  }
}

static int
sh_flush (void *handle)
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
  default:
    return -1;
  }
}

static int
sh_trim (void *handle, uint32_t count, uint64_t offset)
{
  char *h = handle;
  char cbuf[32], obuf[32];
  const char *args[] = { script, "trim", h, cbuf, obuf, NULL };

  snprintf (cbuf, sizeof cbuf, "%" PRIu32, count);
  snprintf (obuf, sizeof obuf, "%" PRIu64, offset);

  switch (call (args)) {
  case OK:
    return 0;

  case MISSING:
    /* Ignore lack of flush callback. */
    return 0;

  case ERROR:
  default:
    return -1;
  }
}

static int
sh_zero (void *handle, uint32_t count, uint64_t offset, int may_trim)
{
  char *h = handle;
  char cbuf[32], obuf[32];
  const char *args[] = { script, "zero", h, cbuf, obuf,
                         may_trim ? "true" : "false", NULL };

  snprintf (cbuf, sizeof cbuf, "%" PRIu32, count);
  snprintf (obuf, sizeof obuf, "%" PRIu64, offset);

  switch (call (args)) {
  case OK:
    return 0;

  case MISSING:
    nbdkit_debug ("zero falling back to pwrite");
    nbdkit_set_error (EOPNOTSUPP);
    return -1;

  case ERROR:
  default:
    return -1;
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

  .pread             = sh_pread,
  .pwrite            = sh_pwrite,
  .flush             = sh_flush,
  .trim              = sh_trim,
  .zero              = sh_zero,

  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
