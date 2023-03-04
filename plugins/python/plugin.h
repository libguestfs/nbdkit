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

#ifndef NBDKIT_PYTHON_PLUGIN_H
#define NBDKIT_PYTHON_PLUGIN_H

/* Include these headers from one place because we have to make sure
 * we always set these #defines identically before inclusion.
 */
#define PY_SSIZE_T_CLEAN 1
#include <Python.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

/* All callbacks that want to call any Py* function should use this
 * macro.  See
 * https://docs.python.org/3/c-api/init.html#non-python-created-threads
 */
#define ACQUIRE_PYTHON_GIL_FOR_CURRENT_SCOPE            \
  __attribute__ ((cleanup (cleanup_release)))           \
  CLANG_UNUSED_VARIABLE_WORKAROUND                      \
  PyGILState_STATE gstate = PyGILState_Ensure ()
static inline void
cleanup_release (PyGILState_STATE *gstateptr)
{
  PyGILState_Release (*gstateptr);
}

extern const char *script;
extern PyObject *module;
extern int py_api_version;
extern __thread int last_error;

/* helpers.c */
extern int callback_defined (const char *name, PyObject **obj_rtn);
extern char *python_to_string (PyObject *str);

/* errors.c */
extern int check_python_failure (const char *callback);

/* modfunctions.c */
extern PyMODINIT_FUNC create_nbdkit_module (void);

#endif /* NBDKIT_PYTHON_PLUGIN_H */
