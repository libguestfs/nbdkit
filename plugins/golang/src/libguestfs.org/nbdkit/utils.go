/* cgo wrappers.
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

package nbdkit

/*
#cgo pkg-config: nbdkit

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

// cgo cannot call varargs functions.
void
_nbdkit_debug (const char *s)
{
  nbdkit_debug ("%s", s);
}

// cgo cannot call varargs functions.
void
_nbdkit_error (const char *s)
{
  nbdkit_error ("%s", s);
}
*/
import "C"
import "syscall"

// Utility functions.

func Debug(s string) {
	C._nbdkit_debug(C.CString(s))
}

// This function is provided but plugins would rarely need to call
// this explicitly since returning an error from a plugin callback
// will call it implicitly.
func Error(s string) {
	C._nbdkit_error(C.CString(s))
}

// Same applies as for Error().  Callers should not usually need to
// call this.
func SetError(err syscall.Errno) {
	C.nbdkit_set_error(C.int(err))
}
