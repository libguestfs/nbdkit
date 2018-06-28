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
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <tcl.h>

#include <nbdkit-plugin.h>

static Tcl_Interp *interp;
static const char *script;

static void
tcl_load (void)
{
  //Tcl_FindExecutable ("nbdkit");
  interp = Tcl_CreateInterp ();
  if (Tcl_Init (interp) != TCL_OK) {
    nbdkit_error ("cannot initialize Tcl interpreter: %s",
                  Tcl_GetStringResult (interp));
    exit (EXIT_FAILURE);
  }
}

static void
tcl_unload (void)
{
  if (interp)
    Tcl_DeleteInterp (interp);
  Tcl_Finalize ();
}

/* Test if proc was defined by the Tcl code. */
static int
proc_defined (const char *name)
{
  int r;
  Tcl_Obj *cmd;

  cmd = Tcl_NewObj ();
  Tcl_IncrRefCount (cmd);
  Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj ("info", -1));
  Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj ("procs", -1));
  Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj (name, -1));
  r = Tcl_EvalObjEx (interp, cmd, TCL_EVAL_DIRECT);
  Tcl_DecrRefCount (cmd);
  if (r != TCL_OK) {
    nbdkit_error ("info procs: %s", Tcl_GetStringResult (interp));
    return 0; /* We can't return an error here, just return false. */
  }

  /* 'info procs name' returns the proc name if it exists, else empty
   * string, so we can just check if the result is not empty.
   */
  return strcmp (Tcl_GetStringResult (interp), "") != 0;
}

static void
tcl_dump_plugin (void)
{
  if (script && proc_defined ("dump_plugin")) {
    int r;
    Tcl_Obj *cmd;

    cmd = Tcl_NewObj ();
    Tcl_IncrRefCount (cmd);
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj ("dump_plugin", -1));
    r = Tcl_EvalObjEx (interp, cmd, TCL_EVAL_DIRECT);
    Tcl_DecrRefCount (cmd);
    if (r != TCL_OK)
      nbdkit_error ("dump_plugin: %s", Tcl_GetStringResult (interp));
  }
}

static int
tcl_config (const char *key, const char *value)
{
  int r;

  if (!script) {
    /* The first parameter MUST be "script". */
    if (strcmp (key, "script") != 0) {
      nbdkit_error ("the first parameter must be script=/path/to/script.tcl");
      return -1;
    }
    script = value;

    assert (interp);

    /* Load the Tcl file. */
    r = Tcl_EvalFile (interp, script);
    if (r != TCL_OK) {
      if (r == TCL_ERROR)
        nbdkit_error ("could not load Tcl script: %s: line %d: %s",
                      script, Tcl_GetErrorLine (interp),
                      Tcl_GetStringResult (interp));
      else
        nbdkit_error ("could not load Tcl script: %s: %s",
                      script, Tcl_GetStringResult (interp));
      return -1;
    }

    /* Minimal set of callbacks which are required (by nbdkit itself). */
    if (!proc_defined ("plugin_open") ||
        !proc_defined ("get_size") ||
        !proc_defined ("pread")) {
      nbdkit_error ("%s: one of the required callbacks 'plugin_open', 'get_size' or 'pread' is not defined by this Tcl script.  nbdkit requires these callbacks.", script);
      return -1;
    }
  }
  else if (proc_defined ("config")) {
    int r;
    Tcl_Obj *cmd;

    cmd = Tcl_NewObj ();
    Tcl_IncrRefCount (cmd);
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj ("config", -1));
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj (key, -1));
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj (value, -1));
    r = Tcl_EvalObjEx (interp, cmd, TCL_EVAL_DIRECT);
    Tcl_DecrRefCount (cmd);
    if (r != TCL_OK) {
      nbdkit_error ("config: %s", Tcl_GetStringResult (interp));
      return -1;
    }
  }
  else {
    /* Emulate what core nbdkit does if a config callback is NULL. */
    nbdkit_error ("%s: this plugin does not need command line configuration",
                  script);
    return -1;
  }

  return 0;
}

static int
tcl_config_complete (void)
{
  int r;
  Tcl_Obj *cmd;

  if (proc_defined ("config_complete")) {
    cmd = Tcl_NewObj ();
    Tcl_IncrRefCount (cmd);
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj ("config_complete", -1));
    r = Tcl_EvalObjEx (interp, cmd, TCL_EVAL_DIRECT);
    Tcl_DecrRefCount (cmd);
    if (r != TCL_OK) {
      nbdkit_error ("config_complete: %s", Tcl_GetStringResult (interp));
      return -1;
    }
  }

  return 0;
}

static void *
tcl_open (int readonly)
{
  int r;
  Tcl_Obj *cmd, *res;

  cmd = Tcl_NewObj ();
  Tcl_IncrRefCount (cmd);
  Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj ("plugin_open", -1));
  Tcl_ListObjAppendElement (0, cmd, Tcl_NewBooleanObj (readonly));
  r = Tcl_EvalObjEx (interp, cmd, TCL_EVAL_DIRECT);
  Tcl_DecrRefCount (cmd);
  if (r != TCL_OK) {
    nbdkit_error ("plugin_open: %s", Tcl_GetStringResult (interp));
    return NULL;
  }

  res = Tcl_GetObjResult (interp);
  Tcl_IncrRefCount (res);
  return res;
}

static void
tcl_close (void *handle)
{
  int r;
  Tcl_Obj *h = handle, *cmd;

  if (proc_defined ("plugin_close")) {
    cmd = Tcl_NewObj ();
    Tcl_IncrRefCount (cmd);
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj ("plugin_close", -1));
    Tcl_ListObjAppendElement (0, cmd, h);
    r = Tcl_EvalObjEx (interp, cmd, TCL_EVAL_DIRECT);
    Tcl_DecrRefCount (cmd);
    if (r != TCL_OK)
      nbdkit_error ("plugin_close: %s", Tcl_GetStringResult (interp));
  }

  /* Ensure that the handle is freed. */
  Tcl_DecrRefCount (h);
}

static int64_t
tcl_get_size (void *handle)
{
  int r;
  Tcl_Obj *h = handle, *cmd, *res;
  Tcl_WideInt size;

  cmd = Tcl_NewObj ();
  Tcl_IncrRefCount (cmd);
  Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj ("get_size", -1));
  Tcl_ListObjAppendElement (0, cmd, h);
  r = Tcl_EvalObjEx (interp, cmd, TCL_EVAL_DIRECT);
  Tcl_DecrRefCount (cmd);
  if (r != TCL_OK) {
    nbdkit_error ("get_size: %s", Tcl_GetStringResult (interp));
    return -1;
  }

  res = Tcl_GetObjResult (interp);
  if (Tcl_GetWideIntFromObj (interp, res, &size) != TCL_OK) {
    nbdkit_error ("get_size: Tcl_GetWideIntFromObj: %s",
                  Tcl_GetStringResult (interp));
    return -1;
  }
  return size;
}

static int
tcl_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  int r;
  Tcl_Obj *h = handle, *cmd, *res;
  unsigned char *res_bin;
  int res_len;

  cmd = Tcl_NewObj ();
  Tcl_IncrRefCount (cmd);
  Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj ("pread", -1));
  Tcl_ListObjAppendElement (0, cmd, h);
  Tcl_ListObjAppendElement (0, cmd, Tcl_NewIntObj (count));
  Tcl_ListObjAppendElement (0, cmd, Tcl_NewWideIntObj (offset));
  r = Tcl_EvalObjEx (interp, cmd, TCL_EVAL_DIRECT);
  Tcl_DecrRefCount (cmd);
  if (r != TCL_OK) {
    nbdkit_error ("pread: %s", Tcl_GetStringResult (interp));
    return -1;
  }

  res = Tcl_GetObjResult (interp);
  res_bin = Tcl_GetByteArrayFromObj (res, &res_len);
  if (res_len < count) {
    nbdkit_error ("pread: buffer returned from pread is too small");
    return -1;
  }

  memcpy (buf, res_bin, count);
  return 0;
}

static int
tcl_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset)
{
  if (proc_defined ("pwrite")) {
    int r;
    Tcl_Obj *h = handle, *cmd;

    cmd = Tcl_NewObj ();
    Tcl_IncrRefCount (cmd);
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj ("pwrite", -1));
    Tcl_ListObjAppendElement (0, cmd, h);
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewByteArrayObj (buf, count));
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewWideIntObj (offset));
    r = Tcl_EvalObjEx (interp, cmd, TCL_EVAL_DIRECT);
    Tcl_DecrRefCount (cmd);
    if (r != TCL_OK) {
      nbdkit_error ("pwrite: %s", Tcl_GetStringResult (interp));
      return -1;
    }
    return 0;
  }

  nbdkit_error ("pwrite not implemented");
  return -1;
}

static int
tcl_can_write (void *handle)
{
  if (proc_defined ("can_write")) {
    int r;
    Tcl_Obj *h = handle, *cmd, *res;

    cmd = Tcl_NewObj ();
    Tcl_IncrRefCount (cmd);
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj ("can_write", -1));
    Tcl_ListObjAppendElement (0, cmd, h);
    r = Tcl_EvalObjEx (interp, cmd, TCL_EVAL_DIRECT);
    Tcl_DecrRefCount (cmd);
    if (r != TCL_OK) {
      nbdkit_error ("can_write: %s", Tcl_GetStringResult (interp));
      return -1;
    }
    res = Tcl_GetObjResult (interp);
    Tcl_GetBooleanFromObj (interp, res, &r);
    return r;
  }
  /* No can_write callback, but there's a pwrite callback defined, so
   * return 1.  (In C modules, nbdkit would do this).
   */
  else if (proc_defined ("pwrite"))
    return 1;
  else
    return 0;
}

static int
tcl_can_flush (void *handle)
{
  if (proc_defined ("can_flush")) {
    int r;
    Tcl_Obj *h = handle, *cmd, *res;

    cmd = Tcl_NewObj ();
    Tcl_IncrRefCount (cmd);
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj ("can_flush", -1));
    Tcl_ListObjAppendElement (0, cmd, h);
    r = Tcl_EvalObjEx (interp, cmd, TCL_EVAL_DIRECT);
    Tcl_DecrRefCount (cmd);
    if (r != TCL_OK) {
      nbdkit_error ("can_flush: %s", Tcl_GetStringResult (interp));
      return -1;
    }
    res = Tcl_GetObjResult (interp);
    Tcl_GetBooleanFromObj (interp, res, &r);
    return r;
  }
  /* No can_flush callback, but there's a plugin_flush callback
   * defined, so return 1.  (In C modules, nbdkit would do this).
   */
  else if (proc_defined ("plugin_flush"))
    return 1;
  else
    return 0;
}

static int
tcl_can_trim (void *handle)
{
  if (proc_defined ("can_trim")) {
    int r;
    Tcl_Obj *h = handle, *cmd, *res;

    cmd = Tcl_NewObj ();
    Tcl_IncrRefCount (cmd);
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj ("can_trim", -1));
    Tcl_ListObjAppendElement (0, cmd, h);
    r = Tcl_EvalObjEx (interp, cmd, TCL_EVAL_DIRECT);
    Tcl_DecrRefCount (cmd);
    if (r != TCL_OK) {
      nbdkit_error ("can_trim: %s", Tcl_GetStringResult (interp));
      return -1;
    }
    res = Tcl_GetObjResult (interp);
    Tcl_GetBooleanFromObj (interp, res, &r);
    return r;
  }
  /* No can_trim callback, but there's a trim callback defined, so
   * return 1.  (In C modules, nbdkit would do this).
   */
  else if (proc_defined ("trim"))
    return 1;
  else
    return 0;
}

static int
tcl_zero (void *handle, uint32_t count, uint64_t offset, int may_trim)
{
  if (proc_defined ("zero")) {
    int r;
    Tcl_Obj *h = handle, *cmd, *res;

    cmd = Tcl_NewObj ();
    Tcl_IncrRefCount (cmd);
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj ("can_zero", -1));
    Tcl_ListObjAppendElement (0, cmd, h);
    r = Tcl_EvalObjEx (interp, cmd, TCL_EVAL_DIRECT);
    Tcl_DecrRefCount (cmd);
    if (r != TCL_OK) {
      nbdkit_error ("can_zero: %s", Tcl_GetStringResult (interp));
      return -1;
    }
    res = Tcl_GetObjResult (interp);
    Tcl_GetBooleanFromObj (interp, res, &r);
    return r;
  }

  nbdkit_debug ("zero falling back to pwrite");
  nbdkit_set_error (EOPNOTSUPP);
  return -1;
}

static int
tcl_is_rotational (void *handle)
{
  if (proc_defined ("is_rotational")) {
    int r;
    Tcl_Obj *h = handle, *cmd, *res;

    cmd = Tcl_NewObj ();
    Tcl_IncrRefCount (cmd);
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj ("is_rotational", -1));
    Tcl_ListObjAppendElement (0, cmd, h);
    r = Tcl_EvalObjEx (interp, cmd, TCL_EVAL_DIRECT);
    Tcl_DecrRefCount (cmd);
    if (r != TCL_OK) {
      nbdkit_error ("is_rotational: %s", Tcl_GetStringResult (interp));
      return -1;
    }
    res = Tcl_GetObjResult (interp);
    Tcl_GetBooleanFromObj (interp, res, &r);
    return r;
  }
  else
    return 0;
}

static int
tcl_flush (void *handle)
{
  if (proc_defined ("plugin_flush")) {
    int r;
    Tcl_Obj *h = handle, *cmd;

    cmd = Tcl_NewObj ();
    Tcl_IncrRefCount (cmd);
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj ("plugin_flush", -1));
    Tcl_ListObjAppendElement (0, cmd, h);
    r = Tcl_EvalObjEx (interp, cmd, TCL_EVAL_DIRECT);
    Tcl_DecrRefCount (cmd);
    if (r != TCL_OK) {
      nbdkit_error ("plugin_flush: %s", Tcl_GetStringResult (interp));
      return -1;
    }
    return 0;
  }

  /* Ignore lack of flush callback, although probably nbdkit will
   * never call this since .can_flush returns false.
   */
  return 0;
}

static int
tcl_trim (void *handle, uint32_t count, uint64_t offset)
{
  if (proc_defined ("trim")) {
    int r;
    Tcl_Obj *h = handle, *cmd;

    cmd = Tcl_NewObj ();
    Tcl_IncrRefCount (cmd);
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewStringObj ("trim", -1));
    Tcl_ListObjAppendElement (0, cmd, h);
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewIntObj (count));
    Tcl_ListObjAppendElement (0, cmd, Tcl_NewWideIntObj (offset));
    r = Tcl_EvalObjEx (interp, cmd, TCL_EVAL_DIRECT);
    Tcl_DecrRefCount (cmd);
    if (r != TCL_OK) {
      nbdkit_error ("trim: %s", Tcl_GetStringResult (interp));
      return -1;
    }
    return 0;
  }

  /* Ignore lack of trim callback, although probably nbdkit will never
   * call this since .can_trim returns false.
   */
  return 0;
}

#define tcl_config_help \
  "script=<FILENAME>     (required) The Tcl script to run.\n" \
  "[other arguments may be used by the plugin that you load]"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

static struct nbdkit_plugin plugin = {
  .name              = "tcl",
  .version           = PACKAGE_VERSION,

  .load              = tcl_load,
  .unload            = tcl_unload,
  .dump_plugin       = tcl_dump_plugin,

  .config            = tcl_config,
  .config_complete   = tcl_config_complete,
  .config_help       = tcl_config_help,

  .open              = tcl_open,
  .close             = tcl_close,

  .get_size          = tcl_get_size,
  .can_write         = tcl_can_write,
  .can_flush         = tcl_can_flush,
  .is_rotational     = tcl_is_rotational,
  .can_trim          = tcl_can_trim,

  .pread             = tcl_pread,
  .pwrite            = tcl_pwrite,
  .flush             = tcl_flush,
  .trim              = tcl_trim,
  .zero              = tcl_zero,
};

NBDKIT_REGISTER_PLUGIN(plugin)
