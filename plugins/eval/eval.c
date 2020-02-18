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

static char tmpdir[] = "/tmp/nbdkitevalXXXXXX";
static char *missing;

static const char *known_methods[] = {
  "cache",
  "can_cache",
  "can_extents",
  "can_fast_zero",
  "can_flush",
  "can_fua",
  "can_multi_conn",
  "can_trim",
  "can_write",
  "can_zero",
  "close",
  "config",
  "config_complete",
  "dump_plugin",
  "extents",
  "flush",
  "get_size",
  "is_rotational",
  "missing",
  "open",
  "pread",
  "pwrite",
  "thread_model",
  "trim",
  "unload",
  "zero",
  NULL
};

/* List of method scripts that we have saved.  This is stored in
 * sorted order of method name.
 */
static size_t nr_method_scripts;
static struct method_script {
  const char *method;
  char *script;
} *method_scripts;

static int
compare_script (const void *methodvp, const void *entryvp)
{
  const char *method = methodvp;
  const struct method_script *entry = entryvp;
  return strcmp (method, entry->method);
}

static int
insert_method_script (const char *method, char *script)
{
  struct method_script *newp;
  size_t i;
  int r;

  newp = realloc (method_scripts,
                  sizeof (struct method_script) * (nr_method_scripts + 1));
  if (newp == NULL) {
    nbdkit_error ("insert_method_script: realloc: %m");
    return -1;
  }
  nr_method_scripts++;
  method_scripts = newp;

  for (i = 0; i < nr_method_scripts-1; ++i) {
    r = compare_script (method, &method_scripts[i]);
    /* This shouldn't happen.  insert_method_script() must not be
     * called if the method has already been added.  Call get_script()
     * first to check.
     */
    assert (r != 0);
    if (r < 0) {
      /* Insert before this element. */
      memmove (&method_scripts[i+1], &method_scripts[i],
               sizeof (struct method_script) * (nr_method_scripts-i-1));
      method_scripts[i].method = method;
      method_scripts[i].script = script;
      return 0;
    }
  }

  /* Insert at end of list. */
  method_scripts[i].method = method;
  method_scripts[i].script = script;
  return 0;
}

/* This is called back by methods.c to get the current script name. */
const char *
get_script (const char *method)
{
  struct method_script *p;

  p = bsearch (method,
               method_scripts,
               nr_method_scripts, sizeof (struct method_script),
               compare_script);
  if (p)
    return p->script;
  else
    return missing;
}

/* Save a script into tmpdir.  Return its full path (must be freed by
 * the caller).
 */
static char *
create_script (const char *method, const char *value)
{
  FILE *fp;
  char *script;
  size_t len;

  if (asprintf (&script, "%s/%s", tmpdir, method) == -1) {
    nbdkit_error ("asprintf: %m");
    return NULL;
  }

  /* Special case for user override of missing */
  if (missing && strcmp (script, missing) == 0 && unlink (script) == -1) {
    nbdkit_error ("unlink: %m");
    return NULL;
  }

  fp = fopen (script, "w");
  if (fp == NULL) {
    nbdkit_error ("fopen: %s: %m", script);
    free (script);
    return NULL;
  }
  len = strlen (value);
  if (len > 0) {
    if (fwrite (value, strlen (value), 1, fp) != 1) {
      nbdkit_error ("fwrite: %s: %m", script);
      fclose (fp);
      free (script);
      return NULL;
    }
  }

  if (fchmod (fileno (fp), 0500) == -1) {
    nbdkit_error ("fchmod: %s: %m", script);
    fclose (fp);
    free (script);
    return NULL;
  }

  if (fclose (fp) == EOF) {
    nbdkit_error ("fclose: %s: %m", script);
    free (script);
    return NULL;
  }

  return script;
}

static void
eval_load (void)
{
  /* Create the temporary directory for the shell script to use. */
  if (mkdtemp (tmpdir) == NULL) {
    nbdkit_error ("mkdtemp: /tmp: %m");
    exit (EXIT_FAILURE);
  }
  /* Set $tmpdir for the script. */
  if (setenv ("tmpdir", tmpdir, 1) == -1) {
    nbdkit_error ("setenv: tmpdir=%s: %m", tmpdir);
    exit (EXIT_FAILURE);
  }

  nbdkit_debug ("eval: load: tmpdir: %s", tmpdir);

  /* To make things easier, create a "missing" script which always
   * exits with code 2.  If a method is missing we call this script
   * instead.  It can even be overridden by the user.
   */
  missing = create_script ("missing", "exit 2\n");
  if (!missing)
    exit (EXIT_FAILURE);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
static void
eval_unload (void)
{
  const char *method = "unload";
  const char *script = get_script (method);
  CLEANUP_FREE char *cmd = NULL;
  size_t i;

  /* Run the unload method.  Ignore all errors. */
  if (script) {
    const char *args[] = { script, method, NULL };

    call (args);
  }

  /* Delete the temporary directory.  Ignore all errors. */
  if (asprintf (&cmd, "rm -rf %s", tmpdir) >= 0)
    system (cmd);

  for (i = 0; i < nr_method_scripts; ++i)
    free (method_scripts[i].script);
  free (method_scripts);
  free (missing);
}
#pragma GCC diagnostic pop

static int
add_method (const char *key, const char *value)
{
  char *script;
  char *tmp = missing; /* Needed to allow user override of missing */

  missing = NULL;
  if (get_script (key) != NULL) {
    missing = tmp;
    nbdkit_error ("method %s defined more than once on the command line", key);
    return -1;
  }
  missing = tmp;

  /* Do a bit of checking to make sure the key isn't malicious.  This
   * duplicates work already done by nbdkit, but better safe than
   * sorry.
   */
  if (strchr (key, '.') || strchr (key, '/')) {
    nbdkit_error ("method name %s is invalid", key);
    return -1;
  }

  /* Copy the value into a script in tmpdir. */
  script = create_script (key, value);
  if (!script)
    return -1;

  /* After this, the script variable will be stored in the global
   * array and freed on unload.
   */
  return insert_method_script (key, script);
}

static int
eval_config (const char *key, const char *value)
{
  size_t i;

  /* Try to determine if this is a method or a user parameter. */
  for (i = 0; known_methods[i] != NULL; ++i) {
    if (strcmp (key, known_methods[i]) == 0)
      return add_method (key, value);
  }

  /* User parameter, so call config. */
  const char *method = "config";
  const char *script = get_script (method);
  const char *args[] = { script, method, key, value, NULL };

  switch (call (args)) {
  case OK:
    return 0;

  case MISSING:
    /* Emulate what core nbdkit does if a config callback is NULL. */
    nbdkit_error ("%s: callback '%s' is unknown, and there is no 'config' "
                  "callback to handle it", script, key);
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

static int
create_can_wrapper (const char *test_method, const char *can_method)
{
  char *can_script;

  if (get_script (test_method) != missing &&
      get_script (can_method) == missing) {
    can_script = create_script (can_method, "exit 0\n");
    if (!can_script)
      return -1;
    return insert_method_script (can_method, can_script);
  }

  return 0;
}

static int
eval_config_complete (void)
{
  const char *method = "config_complete";
  const char *script = get_script (method);
  const char *args[] = { script, method, NULL };

  /* Synthesize can_* scripts as the core nbdkit server would for C
   * plugins.
   */
  if (create_can_wrapper ("pwrite",  "can_write") == -1 ||
      create_can_wrapper ("flush",   "can_flush") == -1 ||
      create_can_wrapper ("trim",    "can_trim") == -1 ||
      create_can_wrapper ("zero",    "can_zero") == -1 ||
      create_can_wrapper ("extents", "can_flush") == -1)
    return -1;

  /* Call config_complete. */
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

  return 0;
}

#define eval_config_help \
  "get_size=' SCRIPT '\n" \
  "pread=' SCRIPT '\n" \
  "[etc]"

/* See also the comments in call.c:call3() */
#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

static struct nbdkit_plugin plugin = {
  .name              = "eval",
  .version           = PACKAGE_VERSION,
  .load              = eval_load,
  .unload            = eval_unload,

  .dump_plugin       = sh_dump_plugin,

  .config            = eval_config,
  .config_complete   = eval_config_complete,
  .config_help       = eval_config_help,
  .thread_model      = sh_thread_model,

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
