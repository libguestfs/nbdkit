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
#include <sys/stat.h>

#define NBDKIT_API_VERSION 2

#include <nbdkit-plugin.h>

#include "cleanup.h"

#include "call.h"
#include "methods.h"

static char *script;
static char *magic_config_key;

/* This is called back by methods.c to get the current script name (if
 * set).  The method parameter is ignored by nbdkit-sh-plugin.
 */
const char *
get_script (const char *method)
{
  return script;
}

static void
sh_load (void)
{
  call_load ();
}

static void
sh_unload (void)
{
  const char *method = "unload";

  /* Run the unload method.  Ignore all errors. */
  if (script) {
    const char *args[] = { script, method, NULL };

    call (args);
  }

  call_unload ();
  free (script);
  free (magic_config_key);
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
  CLEANUP_FREE char *cmd = NULL;

  if (!nbdkit_stdio_safe ()) {
    nbdkit_error ("inline script is incompatible with -s");
    return NULL;
  }

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

  return filename;

 err:
  free (filename);
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

    /* Call the magic_config_key method if it exists. */
    const char *args2[] = { script, "magic_config_key", NULL };
    CLEANUP_FREE char *s = NULL;
    size_t slen;
    switch (call_read (&s, &slen, args2)) {
    case OK:
      if (slen > 0 && s[slen-1] == '\n')
        s[slen-1] = '\0';
      magic_config_key = strdup (s);
      if (magic_config_key == NULL) {
        nbdkit_error ("strdup: %m");
        return -1;
      }
      break;

    case MISSING:
      break;

    case ERROR:
      return -1;

    case RET_FALSE:
      nbdkit_error ("%s: %s method returned unexpected code (3/false)",
                    script, "magic_config_key");
      errno = EIO;
      return -1;

    default: abort ();
    }
  }
  else {
    /* If the script sets a magic_config_key then it's possible that
     * we will be called here with key == "script" (which is the
     * plugin.magic_config_key).  If that happens then swap in the
     * script magic_config_key as the key.  However if the script
     * didn't define a magic_config_key then it's an error, emulating
     * the behaviour of the core server.
     */
    if (strcmp (key, "script") == 0) {
      if (magic_config_key)
        key = magic_config_key;
      else {
        nbdkit_error ("%s: expecting key=value on the command line but got: "
                      "%s\n",
                      script, value);
        return -1;
      }
    }

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

#define sh_config_help \
  "script=<FILENAME>     (required) The shell script to run.\n" \
  "[other arguments may be used by the plugin that you load]"

/* See also the comments in call.c:call3() */
#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

static struct nbdkit_plugin plugin = {
  .name              = "sh",
  .version           = PACKAGE_VERSION,
  .load              = sh_load,
  .unload            = sh_unload,

  .dump_plugin       = sh_dump_plugin,

  .config            = sh_config,
  .config_complete   = sh_config_complete,
  .config_help       = sh_config_help,
  .magic_config_key  = "script",
  .thread_model      = sh_thread_model,
  .get_ready         = sh_get_ready,

  .preconnect        = sh_preconnect,
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
  .can_cache         = sh_can_cache,
  .can_fast_zero     = sh_can_fast_zero,

  .pread             = sh_pread,
  .pwrite            = sh_pwrite,
  .flush             = sh_flush,
  .trim              = sh_trim,
  .zero              = sh_zero,
  .extents           = sh_extents,
  .cache             = sh_cache,

  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
