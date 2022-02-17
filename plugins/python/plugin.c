/* nbdkit
 * Copyright (C) 2013-2021 Red Hat Inc.
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
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>

#include "cleanup.h"

#include "plugin.h"

/* XXX Apparently global state is technically wrong in Python 3, see:
 *
 * https://www.python.org/dev/peps/pep-3121/
 *
 * However it probably doesn't matter for nbdkit since we don't ever
 * have multiple Python interpreters or multiple instances of the
 * plugin in a single process.
 */
const char *script;             /* The Python script name. */
PyObject *module;               /* The imported __main__ module from script. */
int py_api_version = 1;         /* The declared Python API version. */

static PyThreadState *tstate;

static void
py_load (void)
{
  PyImport_AppendInittab ("nbdkit", create_nbdkit_module);
  Py_Initialize ();
  tstate = PyEval_SaveThread ();
}

static void
py_unload (void)
{
  if (tstate) {
    PyEval_RestoreThread (tstate);
    Py_XDECREF (module);
    Py_Finalize ();
  }
}

static void
py_dump_plugin (void)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  PyObject *fn;
  PyObject *r;

  /* Python version and ABI. */
  printf ("python_version=%s\n", PY_VERSION);
  printf ("python_pep_384_abi_version=%d\n", PYTHON_ABI_VERSION);

  /* Maximum nbdkit API version supported. */
  printf ("nbdkit_python_maximum_api_version=%d\n", NBDKIT_API_VERSION);

  /* If the script has a dump_plugin function, call it. */
  if (script && callback_defined ("dump_plugin", &fn)) {
    PyErr_Clear ();

    r = PyObject_CallObject (fn, NULL);
    Py_DECREF (fn);
    Py_DECREF (r);
  }
}

static int
get_py_api_version (void)
{
  PyObject *obj;
  long value;

  obj = PyObject_GetAttrString (module, "API_VERSION");
  if (obj == NULL)
    return 1;                   /* Default to API version 1. */

  value = PyLong_AsLong (obj);
  Py_DECREF (obj);

  if (value < 1 || value > NBDKIT_API_VERSION) {
    nbdkit_error ("%s: API_VERSION requested unknown version: %ld.  "
                  "This plugin supports API versions between 1 and %d.",
                  script, value, NBDKIT_API_VERSION);
    return -1;
  }

  nbdkit_debug ("module requested API_VERSION %ld", value);
  return (int) value;
}

static int
py_config (const char *key, const char *value)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  int fd;
  FILE *fp;
  PyObject *modname;
  PyObject *fn;
  PyObject *r;

  if (!script) {
    /* The first parameter MUST be "script". */
    if (strcmp (key, "script") != 0) {
      nbdkit_error ("the first parameter must be "
                    "script=/path/to/python/script.py");
      return -1;
    }
    script = value;

    /* Load the Python script. Mark the file CLOEXEC, in case the act
     * of loading the script invokes code that in turn fork()s.
     * However, we can't rely on fopen("re"), so do it by hand.  This
     * does not have to be atomic, because there are no threads during
     * .config before the python interpreter is running, but it's
     * easier to use open/fdopen than fopen/fcntl(fileno).
     */
    fd = open (script, O_CLOEXEC | O_RDONLY);
    if (fd == -1) {
      nbdkit_error ("%s: cannot open file: %m", script);
      return -1;
    }
    fp = fdopen (fd, "r");
    if (!fp) {
      nbdkit_error ("%s: cannot open file: %m", script);
      close (fd);
      return -1;
    }

    if (PyRun_SimpleFileEx (fp, script, 1) == -1) {
      nbdkit_error ("%s: error running this script", script);
      return -1;
    }
    /* Note that because closeit flag == 1, fp is now closed. */

    /* The script should define a module called __main__. */
    modname = PyUnicode_FromString ("__main__");
    module = PyImport_Import (modname);
    Py_DECREF (modname);
    if (!module) {
      nbdkit_error ("%s: cannot find __main__ module", script);
      return -1;
    }

    /* Minimal set of callbacks which are required (by nbdkit itself). */
    if (!callback_defined ("open", NULL) ||
        !callback_defined ("get_size", NULL) ||
        !callback_defined ("pread", NULL)) {
      nbdkit_error ("%s: one of the required callbacks "
                    "'open', 'get_size' or 'pread' "
                    "is not defined by this Python script.  "
                    "nbdkit requires these callbacks.", script);
      return -1;
    }

    /* Get the API version. */
    py_api_version = get_py_api_version ();
    if (py_api_version == -1)
      return -1;
  }
  else if (callback_defined ("config", &fn)) {
    /* Other parameters are passed to the Python .config callback. */
    PyErr_Clear ();

    r = PyObject_CallFunction (fn, "ss", key, value);
    Py_DECREF (fn);
    if (check_python_failure ("config") == -1)
      return -1;
    Py_DECREF (r);
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
py_config_complete (void)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  PyObject *fn;
  PyObject *r;

  if (callback_defined ("config_complete", &fn)) {
    PyErr_Clear ();

    r = PyObject_CallObject (fn, NULL);
    Py_DECREF (fn);
    if (check_python_failure ("config_complete") == -1)
      return -1;
    Py_DECREF (r);
  }

  return 0;
}

static int
py_thread_model (void)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  PyObject *fn;
  PyObject *r;
  int ret = NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS;

  if (script && callback_defined ("thread_model", &fn)) {
    PyErr_Clear ();

    r = PyObject_CallObject (fn, NULL);
    Py_DECREF (fn);
    if (check_python_failure ("thread_model") == -1)
      return -1;
    ret = PyLong_AsLong (r);
    Py_DECREF (r);
  }

  return ret;
}

static int
py_get_ready (void)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  PyObject *fn;
  PyObject *r;

  if (callback_defined ("get_ready", &fn)) {
    PyErr_Clear ();

    r = PyObject_CallObject (fn, NULL);
    Py_DECREF (fn);
    if (check_python_failure ("get_ready") == -1)
      return -1;
    Py_DECREF (r);
  }

  return 0;
}

static int
py_after_fork (void)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  PyObject *fn;
  PyObject *r;

  if (callback_defined ("after_fork", &fn)) {
    PyErr_Clear ();

    r = PyObject_CallObject (fn, NULL);
    Py_DECREF (fn);
    if (check_python_failure ("after_fork") == -1)
      return -1;
    Py_DECREF (r);
  }

  return 0;
}

static void
py_cleanup (void)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  PyObject *fn;
  PyObject *r;

  if (callback_defined ("cleanup", &fn)) {
    PyErr_Clear ();

    r = PyObject_CallObject (fn, NULL);
    Py_DECREF (fn);
    if (check_python_failure ("cleanup") == -1)
      return;
    Py_DECREF (r);
  }
}

static int
py_list_exports (int readonly, int is_tls, struct nbdkit_exports *exports)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  PyObject *fn;
  PyObject *r;
  PyObject *iter, *t;

  if (!callback_defined ("list_exports", &fn))
    /* Do the same as the core server */
    return nbdkit_use_default_export (exports);

  PyErr_Clear ();

  r = PyObject_CallFunctionObjArgs (fn,
                                    readonly ? Py_True : Py_False,
                                    is_tls ? Py_True : Py_False,
                                    NULL);
  Py_DECREF (fn);
  if (check_python_failure ("list_exports") == -1)
    return -1;

  iter = PyObject_GetIter (r);
  if (iter == NULL) {
    nbdkit_error ("list_exports method did not return "
                  "something which is iterable");
    Py_DECREF (r);
    return -1;
  }

  while ((t = PyIter_Next (iter)) != NULL) {
    PyObject *py_name, *py_desc;
    CLEANUP_FREE char *name = NULL;
    CLEANUP_FREE char *desc = NULL;

    name = python_to_string (t);
    if (!name) {
      if (!PyTuple_Check (t) || PyTuple_Size (t) != 2) {
        nbdkit_error ("list_exports method did not return an iterable of "
                      "2-tuples");
        Py_DECREF (iter);
        Py_DECREF (r);
        return -1;
      }
      py_name = PyTuple_GetItem (t, 0);
      py_desc = PyTuple_GetItem (t, 1);
      name = python_to_string (py_name);
      desc = python_to_string (py_desc);
      if (name == NULL || desc == NULL) {
        nbdkit_error ("list_exports method did not return an iterable of "
                      "string 2-tuples");
        Py_DECREF (iter);
        Py_DECREF (r);
        return -1;
      }
    }
    if (nbdkit_add_export (exports, name, desc) == -1) {
      Py_DECREF (iter);
      Py_DECREF (r);
      return -1;
    }
  }

  Py_DECREF (iter);
  Py_DECREF (r);
  return 0;
}

static const char *
py_default_export (int readonly, int is_tls)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  PyObject *fn;
  PyObject *r;
  CLEANUP_FREE char *name = NULL;

  if (!callback_defined ("default_export", &fn))
    return "";

  PyErr_Clear ();

  r = PyObject_CallFunctionObjArgs (fn,
                                    readonly ? Py_True : Py_False,
                                    is_tls ? Py_True : Py_False,
                                    NULL);
  Py_DECREF (fn);
  if (check_python_failure ("default_export") == -1)
    return NULL;

  name = python_to_string (r);
  Py_DECREF (r);
  if (!name) {
    nbdkit_error ("default_export method did not return a string");
    return NULL;
  }

  return nbdkit_strdup_intern (name);
}

static int
py_preconnect (int readonly)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  PyObject *fn;
  PyObject *r;

  if (callback_defined ("preconnect", &fn)) {
    PyErr_Clear ();

    r = PyObject_CallFunctionObjArgs (fn, readonly ? Py_True : Py_False,
                                      NULL);
    Py_DECREF (fn);
    if (check_python_failure ("preconnect") == -1)
      return -1;
    Py_DECREF (r);
  }

  return 0;
}

struct handle {
  int can_zero;
  PyObject *py_h;
};

static void *
py_open (int readonly)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  PyObject *fn;
  struct handle *h;

  if (!callback_defined ("open", &fn)) {
    nbdkit_error ("%s: missing callback: %s", script, "open");
    return NULL;
  }

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }
  h->can_zero = -1;

  PyErr_Clear ();

  h->py_h = PyObject_CallFunctionObjArgs (fn, readonly ? Py_True : Py_False,
                                          NULL);
  Py_DECREF (fn);
  if (check_python_failure ("open") == -1) {
    free (h);
    return NULL;
  }

  assert (h->py_h);
  return h;
}

static void
py_close (void *handle)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  struct handle *h = handle;
  PyObject *fn;
  PyObject *r;

  if (callback_defined ("close", &fn)) {
    PyErr_Clear ();

    r = PyObject_CallFunctionObjArgs (fn, h->py_h, NULL);
    Py_DECREF (fn);
    check_python_failure ("close");
    Py_XDECREF (r);
  }

  Py_DECREF (h->py_h);
  free (h);
}

static const char *
py_export_description (void *handle)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  struct handle *h = handle;
  PyObject *fn;
  PyObject *r;
  CLEANUP_FREE char *desc = NULL;

  if (!callback_defined ("export_description", &fn))
    return NULL;

  PyErr_Clear ();

  r = PyObject_CallFunctionObjArgs (fn, h->py_h, NULL);
  Py_DECREF (fn);
  if (check_python_failure ("export_description") == -1)
    return NULL;

  desc = python_to_string (r);
  Py_DECREF (r);
  if (!desc) {
    nbdkit_error ("export_description method did not return a string");
    return NULL;
  }

  return nbdkit_strdup_intern (desc);
}

static int64_t
py_get_size (void *handle)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  struct handle *h = handle;
  PyObject *fn;
  PyObject *r;
  int64_t ret;

  if (!callback_defined ("get_size", &fn)) {
    nbdkit_error ("%s: missing callback: %s", script, "get_size");
    return -1;
  }

  PyErr_Clear ();

  r = PyObject_CallFunctionObjArgs (fn, h->py_h, NULL);
  Py_DECREF (fn);
  if (check_python_failure ("get_size") == -1)
    return -1;

  ret = PyLong_AsLongLong (r);
  Py_DECREF (r);
  if (check_python_failure ("PyLong_AsLongLong") == -1)
    return -1;

  return ret;
}

static int
py_block_size (void *handle,
               uint32_t *minimum, uint32_t *preferred, uint32_t *maximum)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  struct handle *h = handle;
  PyObject *fn;
  PyObject *r;
  unsigned u_minimum, u_preferred, u_maximum;

  if (callback_defined ("block_size", &fn)) {
    PyErr_Clear ();

    r = PyObject_CallFunctionObjArgs (fn, h->py_h, NULL);
    Py_DECREF (fn);
    if (check_python_failure ("block_size") == -1)
      return -1;

    PyArg_ParseTuple (r, "III",
                      &u_minimum, &u_preferred, &u_maximum);
    Py_DECREF (r);
    if (check_python_failure ("block_size: PyArg_ParseTuple") == -1)
      return -1;

    *minimum = u_minimum;
    *preferred = u_preferred;
    *maximum = u_maximum;
    return 0;
  }
  else {
    *minimum = *preferred = *maximum = 0;
    return 0;
  }
}

static int
py_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
          uint32_t flags)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  struct handle *h = handle;
  PyObject *fn;
  PyObject *r;
  Py_buffer view = {0};
  int ret = -1;

  if (!callback_defined ("pread", &fn)) {
    nbdkit_error ("%s: missing callback: %s", script, "pread");
    return ret;
  }

  PyErr_Clear ();

  switch (py_api_version) {
  case 1:
    r = PyObject_CallFunction (fn, "OIL", h->py_h, count, offset);
    break;
  case 2:
    r = PyObject_CallFunction (fn, "ONLI", h->py_h,
          PyMemoryView_FromMemory ((char *)buf, count, PyBUF_WRITE),
          offset, flags);
    break;
  default: abort ();
  }
  Py_DECREF (fn);
  if (check_python_failure ("pread") == -1)
    return ret;

  if (py_api_version == 1) {
    /* In API v1 the Python pread function had to return a buffer
     * protocol compatible function.  In API v2+ it writes directly to
     * the C buffer so this code is not used.
     */
    if (PyObject_GetBuffer (r, &view, PyBUF_SIMPLE) == -1) {
      nbdkit_error ("%s: value returned from pread does not support the "
                    "buffer protocol",
                    script);
      goto out;
    }

    if (view.len < count) {
      nbdkit_error ("%s: buffer returned from pread is too small", script);
      goto out;
    }

    memcpy (buf, view.buf, count);
  }
  ret = 0;

out:
  if (view.obj)
    PyBuffer_Release (&view);

  Py_DECREF (r);

  return ret;
}

static int
py_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
           uint32_t flags)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  struct handle *h = handle;
  PyObject *fn;
  PyObject *r;

  if (callback_defined ("pwrite", &fn)) {
    PyErr_Clear ();

    switch (py_api_version) {
    case 1:
      r = PyObject_CallFunction (fn, "ONL", h->py_h,
            PyMemoryView_FromMemory ((char *)buf, count, PyBUF_READ),
            offset);
      break;
    case 2:
      r = PyObject_CallFunction (fn, "ONLI", h->py_h,
            PyMemoryView_FromMemory ((char *)buf, count, PyBUF_READ),
            offset, flags);
      break;
    default: abort ();
    }
    Py_DECREF (fn);
    if (check_python_failure ("pwrite") == -1)
      return -1;
    Py_DECREF (r);
  }
  else {
    nbdkit_error ("%s not implemented", "pwrite");
    return -1;
  }

  return 0;
}

static int
py_flush (void *handle, uint32_t flags)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  struct handle *h = handle;
  PyObject *fn;
  PyObject *r;

  if (callback_defined ("flush", &fn)) {
    PyErr_Clear ();

    switch (py_api_version) {
    case 1:
      r = PyObject_CallFunctionObjArgs (fn, h->py_h, NULL);
      break;
    case 2:
      r = PyObject_CallFunction (fn, "OI", h->py_h, flags);
      break;
    default: abort ();
    }
    Py_DECREF (fn);
    if (check_python_failure ("flush") == -1)
      return -1;
    Py_DECREF (r);
  }
  else {
    nbdkit_error ("%s not implemented", "flush");
    return -1;
  }

  return 0;
}

static int
py_trim (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  struct handle *h = handle;
  PyObject *fn;
  PyObject *r;

  if (callback_defined ("trim", &fn)) {
    PyErr_Clear ();

    switch (py_api_version) {
    case 1:
      r = PyObject_CallFunction (fn, "OIL", h->py_h, count, offset);
      break;
    case 2:
      r = PyObject_CallFunction (fn, "OILI", h->py_h, count, offset, flags);
      break;
    default: abort ();
    }
    Py_DECREF (fn);
    if (check_python_failure ("trim") == -1)
      return -1;
    Py_DECREF (r);
  }
  else {
    nbdkit_error ("%s not implemented", "trim");
    return -1;
  }

  return 0;
}

static int
py_zero (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  struct handle *h = handle;
  PyObject *fn;
  PyObject *r;

  if (callback_defined ("zero", &fn)) {
    PyErr_Clear ();

    last_error = 0;
    switch (py_api_version) {
    case 1: {
      int may_trim = flags & NBDKIT_FLAG_MAY_TRIM;
      r = PyObject_CallFunction (fn, "OILO",
                                 h->py_h, count, offset,
                                 may_trim ? Py_True : Py_False);
      break;
    }
    case 2:
      r = PyObject_CallFunction (fn, "OILI", h->py_h, count, offset, flags);
      break;
    default: abort ();
    }
    Py_DECREF (fn);
    if (last_error == EOPNOTSUPP || last_error == ENOTSUP) {
      /* When user requests this particular error, we want to
       * gracefully fall back, and to accommodate both a normal return
       * and an exception.
       */
      nbdkit_debug ("zero requested falling back to pwrite");
      Py_XDECREF (r);
      PyErr_Clear ();
      return -1;
    }
    if (check_python_failure ("zero") == -1)
      return -1;
    Py_DECREF (r);
    return 0;
  }

  nbdkit_debug ("zero missing, falling back to pwrite");
  nbdkit_set_error (EOPNOTSUPP);
  return -1;
}

static int
py_cache (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  struct handle *h = handle;
  PyObject *fn;
  PyObject *r;

  if (callback_defined ("cache", &fn)) {
    PyErr_Clear ();

    switch (py_api_version) {
    case 1:
    case 2:
      r = PyObject_CallFunction (fn, "OILI",
                                 h->py_h, count, offset, flags, NULL);
      break;
    default: abort ();
    }
    Py_DECREF (fn);
    if (check_python_failure ("cache") == -1)
      return -1;
    Py_DECREF (r);
  }
  else {
    nbdkit_error ("%s not implemented", "cache");
    return -1;
  }

  return 0;
}

static int
boolean_callback (void *handle, const char *can_fn, const char *plain_fn)
{
  struct handle *h = handle;
  PyObject *fn;
  PyObject *r;
  int ret;

  if (callback_defined (can_fn, &fn)) {
    PyErr_Clear ();

    r = PyObject_CallFunctionObjArgs (fn, h->py_h, NULL);
    Py_DECREF (fn);
    if (check_python_failure (can_fn) == -1)
      return -1;
    ret = r == Py_True;
    Py_DECREF (r);
    return ret;
  }
  /* No Python ‘can_fn’ (eg. ‘can_write’), but if there's a Python
   * ‘plain_fn’ (eg. ‘pwrite’) callback defined, return 1.  (In C
   * modules, nbdkit would do this).
   */
  else if (plain_fn && callback_defined (plain_fn, NULL))
    return 1;
  else
    return 0;
}

static int
py_is_rotational (void *handle)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  return boolean_callback (handle, "is_rotational", NULL);
}

static int
py_can_multi_conn (void *handle)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  return boolean_callback (handle, "can_multi_conn", NULL);
}

static int
py_can_write (void *handle)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  return boolean_callback (handle, "can_write", "pwrite");
}

static int
py_can_flush (void *handle)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  return boolean_callback (handle, "can_flush", "flush");
}

static int
py_can_trim (void *handle)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  return boolean_callback (handle, "can_trim", "trim");
}

static int
py_can_zero (void *handle)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  struct handle *h = handle;

  if (h->can_zero >= 0)
    return h->can_zero;
  return h->can_zero = boolean_callback (handle, "can_zero", "zero");
}

static int
py_can_fast_zero (void *handle)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  int r;

  if (callback_defined ("can_fast_zero", NULL))
    return boolean_callback (handle, "can_fast_zero", NULL);

  /* No Python ‘can_fast_zero’, but we advertise fast fail support when
   * 'can_zero' is false.  (In C modules, nbdkit would do this).
   */
  r = py_can_zero (handle);
  if (r == -1)
    return -1;
  return !r;
}

static int
py_can_fua (void *handle)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  struct handle *h = handle;
  PyObject *fn;
  PyObject *r;
  int ret;

  if (callback_defined ("can_fua", &fn)) {
    PyErr_Clear ();

    r = PyObject_CallFunctionObjArgs (fn, h->py_h, NULL);
    Py_DECREF (fn);
    if (check_python_failure ("can_fua") == -1)
      return -1;
    ret = PyLong_AsLong (r);
    Py_DECREF (r);
    return ret;
  }
  /* No Python ‘can_fua’, but check if there's a Python ‘flush’
   * callback defined.  (In C modules, nbdkit would do this).
   */
  else if (callback_defined ("flush", NULL))
    return NBDKIT_FUA_EMULATE;
  else
    return NBDKIT_FUA_NONE;
}

static int
py_can_cache (void *handle)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  struct handle *h = handle;
  PyObject *fn;
  PyObject *r;
  int ret;

  if (callback_defined ("can_cache", &fn)) {
    PyErr_Clear ();

    r = PyObject_CallFunctionObjArgs (fn, h->py_h, NULL);
    Py_DECREF (fn);
    if (check_python_failure ("can_cache") == -1)
      return -1;
    ret = PyLong_AsLong (r);
    Py_DECREF (r);
    return ret;
  }
  /* No Python ‘can_cache’, but check if there's a Python ‘cache’
   * callback defined.  (In C modules, nbdkit would do this).
   */
  else if (callback_defined ("cache", NULL))
    return NBDKIT_CACHE_NATIVE;
  else
    return NBDKIT_CACHE_NONE;
}

static int
py_can_extents (void *handle)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  return boolean_callback (handle, "can_extents", "extents");
}

static int
py_extents (void *handle, uint32_t count, uint64_t offset,
            uint32_t flags, struct nbdkit_extents *extents)
{
  ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE;
  struct handle *h = handle;
  PyObject *fn;
  PyObject *r;
  PyObject *iter, *t;
  size_t size;

  if (callback_defined ("extents", &fn)) {
    PyErr_Clear ();

    r = PyObject_CallFunction (fn, "OILI", h->py_h, count, offset, flags);
    Py_DECREF (fn);
    if (check_python_failure ("extents") == -1)
      return -1;

    iter = PyObject_GetIter (r);
    if (iter == NULL) {
      nbdkit_error ("extents method did not return "
                    "something which is iterable");
      Py_DECREF (r);
      return -1;
    }

    size = 0;
    while ((t = PyIter_Next (iter)) != NULL) {
      PyObject *py_offset, *py_length, *py_type;
      uint64_t extent_offset, extent_length;
      uint32_t extent_type;

      size++;

      if (!PyTuple_Check (t) || PyTuple_Size (t) != 3) {
        nbdkit_error ("extents method did not return an iterable of 3-tuples");
        Py_DECREF (iter);
        Py_DECREF (r);
        return -1;
      }
      py_offset = PyTuple_GetItem (t, 0);
      py_length = PyTuple_GetItem (t, 1);
      py_type = PyTuple_GetItem (t, 2);
      extent_offset = PyLong_AsUnsignedLongLong (py_offset);
      extent_length = PyLong_AsUnsignedLongLong (py_length);
      extent_type = PyLong_AsUnsignedLong (py_type);
      if (check_python_failure ("PyLong") == -1) {
        Py_DECREF (iter);
        Py_DECREF (r);
        return -1;
      }
      if (nbdkit_add_extent (extents,
                             extent_offset, extent_length, extent_type) == -1) {
        Py_DECREF (iter);
        Py_DECREF (r);
        return -1;
      }
    }

    if (size < 1) {
      nbdkit_error ("extents method cannot return an empty list");
      Py_DECREF (iter);
      Py_DECREF (r);
      return -1;
    }

    Py_DECREF (iter);
    Py_DECREF (r);
  }
  else {
    /* Do the same as the core server: synthesize a fully allocated
     * extent covering the whole range.
     */
    if (nbdkit_add_extent (extents,
                           offset, count, 0 /* allocated data */) == -1)
      return -1;
  }

  return 0;
}

#define py_config_help \
  "script=<FILENAME>     (required) The Python plugin to run.\n" \
  "[other arguments may be used by the plugin that you load]"

/* This is the maximum possible, but the default for plugins is
 * NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS.  Plugins can override
 * that by providing a thread_model() function.
 */
#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

static struct nbdkit_plugin plugin = {
  .name               = "python",
  .version            = PACKAGE_VERSION,

  .load               = py_load,
  .unload             = py_unload,
  .dump_plugin        = py_dump_plugin,

  .config             = py_config,
  .config_complete    = py_config_complete,
  .config_help        = py_config_help,

  .thread_model       = py_thread_model,
  .get_ready          = py_get_ready,
  .after_fork         = py_after_fork,
  .cleanup            = py_cleanup,
  .list_exports       = py_list_exports,
  .default_export     = py_default_export,

  .preconnect         = py_preconnect,
  .open               = py_open,
  .close              = py_close,

  .export_description = py_export_description,
  .get_size           = py_get_size,
  .block_size         = py_block_size,
  .is_rotational      = py_is_rotational,
  .can_multi_conn     = py_can_multi_conn,
  .can_write          = py_can_write,
  .can_flush          = py_can_flush,
  .can_trim           = py_can_trim,
  .can_zero           = py_can_zero,
  .can_fast_zero      = py_can_fast_zero,
  .can_fua            = py_can_fua,
  .can_cache          = py_can_cache,
  .can_extents        = py_can_extents,

  .pread              = py_pread,
  .pwrite             = py_pwrite,
  .flush              = py_flush,
  .trim               = py_trim,
  .zero               = py_zero,
  .cache              = py_cache,
  .extents            = py_extents,
};

NBDKIT_REGISTER_PLUGIN (plugin)
