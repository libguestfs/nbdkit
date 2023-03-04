/* nbdkit
 * Copyright Red Hat
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

#include "cleanup.h"

#include "plugin.h"

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

int
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
