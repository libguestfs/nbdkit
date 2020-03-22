/* nbdkit
 * Copyright (C) 2013-2020 Red Hat Inc.
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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>

#define PY_SSIZE_T_CLEAN 1
#include <Python.h>

#define NBDKIT_API_VERSION 2

#include <nbdkit-plugin.h>

#include "cleanup.h"

/* XXX Apparently global state is technically wrong in Python 3, see:
 *
 * https://www.python.org/dev/peps/pep-3121/
 *
 * However it probably doesn't matter for nbdkit since we don't ever
 * have multiple Python interpreters or multiple instances of the
 * plugin in a single process.
 */
static const char *script;
static PyObject *module;
static int py_api_version = 1;

static int last_error;

static PyObject *
set_error (PyObject *self, PyObject *args)
{
  int err;

  if (!PyArg_ParseTuple (args, "i", &err))
    return NULL;
  nbdkit_set_error (err);
  last_error = err;
  Py_RETURN_NONE;
}

static PyMethodDef NbdkitMethods[] = {
  { "set_error", set_error, METH_VARARGS,
    "Store an errno value prior to throwing an exception" },
  { NULL }
};

/* Is a callback defined? */
static int
callback_defined (const char *name, PyObject **obj_rtn)
{
  PyObject *obj;

  assert (script != NULL);
  assert (module != NULL);

  obj = PyObject_GetAttrString (module, name);
  if (!obj) {
    PyErr_Clear (); /* Clear the AttributeError from testing attr. */
    return 0;
  }
  if (!PyCallable_Check (obj)) {
    nbdkit_debug ("object %s isn't callable", name);
    Py_DECREF (obj);
    return 0;
  }

  if (obj_rtn != NULL)
    *obj_rtn = obj;
  else
    Py_DECREF (obj);

  return 1;
}

/* Convert bytes/str/unicode into a string.  Caller must free. */
static char *
python_to_string (PyObject *str)
{
  if (str) {
    if (PyUnicode_Check (str))
      return strdup (PyUnicode_AsUTF8 (str));
    else if (PyBytes_Check (str))
      return strdup (PyBytes_AS_STRING (str));
  }
  return NULL;
}

/* This is the fallback in case we cannot get the full traceback. */
static void
print_python_error (const char *callback, PyObject *error)
{
  PyObject *error_str;
  CLEANUP_FREE char *error_cstr = NULL;

  error_str = PyObject_Str (error);
  error_cstr = python_to_string (error_str);
  nbdkit_error ("%s: %s: error: %s",
                script, callback,
                error_cstr ? error_cstr : "<unknown>");
  Py_DECREF (error_str);
}

/* Convert the Python traceback to a string and call nbdkit_error.
 * https://stackoverflow.com/a/15907460/7126113
 */
static int
print_python_traceback (const char *callback,
                        PyObject *type, PyObject *error, PyObject *traceback)
{
  PyObject *module_name, *traceback_module, *format_exception_fn, *rv,
    *traceback_str;
  CLEANUP_FREE char *traceback_cstr = NULL;

  module_name = PyUnicode_FromString ("traceback");
  traceback_module = PyImport_Import (module_name);
  Py_DECREF (module_name);

  /* couldn't 'import traceback' */
  if (traceback_module == NULL)
    return -1;

  format_exception_fn = PyObject_GetAttrString (traceback_module,
                                                "format_exception");
  if (format_exception_fn == NULL)
    return -1;
  if (!PyCallable_Check (format_exception_fn))
    return -1;

  rv = PyObject_CallFunctionObjArgs (format_exception_fn,
                                     type, error, traceback, NULL);
  if (rv == NULL)
    return -1;
  traceback_str = PyUnicode_Join (NULL, rv);
  Py_DECREF (rv);
  traceback_cstr = python_to_string (traceback_str);
  if (traceback_cstr == NULL) {
    Py_DECREF (traceback_str);
    return -1;
  }

  nbdkit_error ("%s: %s: error: %s",
                script, callback,
                traceback_cstr);
  Py_DECREF (traceback_str);

  /* This means we succeeded in calling nbdkit_error. */
  return 0;
}

static int
check_python_failure (const char *callback)
{
  if (PyErr_Occurred ()) {
    PyObject *type, *error, *traceback;

    PyErr_Fetch (&type, &error, &traceback);
    PyErr_NormalizeException (&type, &error, &traceback);

    /* Try to print the full traceback. */
    if (print_python_traceback (callback, type, error, traceback) == -1) {
      /* Couldn't do that, so fall back to converting the Python error
       * to a string.
       */
      print_python_error (callback, error);
    }

    /* In all cases this returns -1 to indicate that a Python error
     * occurred.
     */
    return -1;
  }
  return 0;
}

static struct PyModuleDef moduledef = {
  PyModuleDef_HEAD_INIT,
  "nbdkit",
  "Module used to access nbdkit server API",
  -1,
  NbdkitMethods,
  NULL,
  NULL,
  NULL,
  NULL
};

PyMODINIT_FUNC
create_nbdkit_module (void)
{
  PyObject *m;

  m = PyModule_Create (&moduledef);
  if (m == NULL) {
    nbdkit_error ("could not create the nbdkit API module");
    exit (EXIT_FAILURE);
  }

  /* Constants corresponding to various flags. */
#define ADD_INT_CONSTANT(name)                                      \
  if (PyModule_AddIntConstant (m, #name, NBDKIT_##name) == -1) {    \
    nbdkit_error ("could not add constant %s to nbdkit API module", \
                  #name);                                           \
    exit (EXIT_FAILURE);                                            \
  }
  ADD_INT_CONSTANT (THREAD_MODEL_SERIALIZE_CONNECTIONS);
  ADD_INT_CONSTANT (THREAD_MODEL_SERIALIZE_ALL_REQUESTS);
  ADD_INT_CONSTANT (THREAD_MODEL_SERIALIZE_REQUESTS);
  ADD_INT_CONSTANT (THREAD_MODEL_PARALLEL);

  ADD_INT_CONSTANT (FLAG_MAY_TRIM);
  ADD_INT_CONSTANT (FLAG_FUA);
  ADD_INT_CONSTANT (FLAG_REQ_ONE);
  ADD_INT_CONSTANT (FLAG_FAST_ZERO);

  ADD_INT_CONSTANT (FUA_NONE);
  ADD_INT_CONSTANT (FUA_EMULATE);
  ADD_INT_CONSTANT (FUA_NATIVE);

  ADD_INT_CONSTANT (CACHE_NONE);
  ADD_INT_CONSTANT (CACHE_EMULATE);
  ADD_INT_CONSTANT (CACHE_NATIVE);

  ADD_INT_CONSTANT (EXTENT_HOLE);
  ADD_INT_CONSTANT (EXTENT_ZERO);
#undef ADD_INT_CONSTANT

  return m;
}

static void
py_load (void)
{
  PyImport_AppendInittab ("nbdkit", create_nbdkit_module);
  Py_Initialize ();
}

static void
py_unload (void)
{
  Py_XDECREF (module);

  Py_Finalize ();
}

static void
py_dump_plugin (void)
{
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
py_get_ready (void)
{
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

struct handle {
  int can_zero;
  PyObject *py_h;
};

static void *
py_open (int readonly)
{
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

static int64_t
py_get_size (void *handle)
{
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
py_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
          uint32_t flags)
{
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
    r = PyObject_CallFunction (fn, "OiL", h->py_h, count, offset);
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
  struct handle *h = handle;
  PyObject *fn;
  PyObject *r;

  if (callback_defined ("trim", &fn)) {
    PyErr_Clear ();

    switch (py_api_version) {
    case 1:
      r = PyObject_CallFunction (fn, "OiL", h->py_h, count, offset);
      break;
    case 2:
      r = PyObject_CallFunction (fn, "OiLI", h->py_h, count, offset, flags);
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
  struct handle *h = handle;
  PyObject *fn;
  PyObject *r;

  if (callback_defined ("zero", &fn)) {
    PyErr_Clear ();

    last_error = 0;
    switch (py_api_version) {
    case 1: {
      int may_trim = flags & NBDKIT_FLAG_MAY_TRIM;
      r = PyObject_CallFunction (fn, "OiLO",
                                 h->py_h, count, offset,
                                 may_trim ? Py_True : Py_False);
      break;
    }
    case 2:
      r = PyObject_CallFunction (fn, "OiLI", h->py_h, count, offset, flags);
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
  struct handle *h = handle;
  PyObject *fn;
  PyObject *r;

  if (callback_defined ("cache", &fn)) {
    PyErr_Clear ();

    switch (py_api_version) {
    case 1:
    case 2:
      r = PyObject_CallFunction (fn, "OiLI", h->py_h, count, offset, flags, NULL);
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
  return boolean_callback (handle, "is_rotational", NULL);
}

static int
py_can_multi_conn (void *handle)
{
  return boolean_callback (handle, "can_multi_conn", NULL);
}

static int
py_can_write (void *handle)
{
  return boolean_callback (handle, "can_write", "pwrite");
}

static int
py_can_flush (void *handle)
{
  return boolean_callback (handle, "can_flush", "flush");
}

static int
py_can_trim (void *handle)
{
  return boolean_callback (handle, "can_trim", "trim");
}

static int
py_can_zero (void *handle)
{
  struct handle *h = handle;

  if (h->can_zero >= 0)
    return h->can_zero;
  return h->can_zero = boolean_callback (handle, "can_zero", "zero");
}

static int
py_can_fast_zero (void *handle)
{
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

#define py_config_help \
  "script=<FILENAME>     (required) The Python plugin to run.\n" \
  "[other arguments may be used by the plugin that you load]"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

static struct nbdkit_plugin plugin = {
  .name              = "python",
  .version           = PACKAGE_VERSION,

  .load              = py_load,
  .unload            = py_unload,
  .dump_plugin       = py_dump_plugin,

  .config            = py_config,
  .config_complete   = py_config_complete,
  .config_help       = py_config_help,

  .get_ready         = py_get_ready,

  .open              = py_open,
  .close             = py_close,

  .get_size          = py_get_size,
  .is_rotational     = py_is_rotational,
  .can_multi_conn    = py_can_multi_conn,
  .can_write         = py_can_write,
  .can_flush         = py_can_flush,
  .can_trim          = py_can_trim,
  .can_zero          = py_can_zero,
  .can_fast_zero     = py_can_fast_zero,
  .can_fua           = py_can_fua,
  .can_cache         = py_can_cache,

  .pread             = py_pread,
  .pwrite            = py_pwrite,
  .flush             = py_flush,
  .trim              = py_trim,
  .zero              = py_zero,
  .cache             = py_cache,
};

NBDKIT_REGISTER_PLUGIN (plugin)
