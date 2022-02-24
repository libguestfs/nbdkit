/* nbdkit
 * Copyright (C) 2018 Red Hat Inc.
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

#ifndef NBDKIT_CALL_H
#define NBDKIT_CALL_H

#include "nbdkit-string.h"

/* eval and sh plugin call this in .load() to initialize some things
 * in the shared call code.  This also creates the tmpdir[] directory.
 */
extern void call_load (void);
extern char tmpdir[];

/* Similarly the plugins should call this in their .unload()
 * functions.  It deletes tmpdir amongst other things.
 */
extern void call_unload (void);

/* Exit codes. */
typedef enum exit_code {
  OK = 0,
  ERROR = 1,           /* all script error codes are mapped to this */
  MISSING = 2,         /* method missing */
  RET_FALSE = 3        /* script exited with code 3 meaning false */
} exit_code;

extern exit_code call (const char **argv)
  __attribute__((__nonnull__ (1)));
extern exit_code call_read (string *rbuf, const char **argv)
  __attribute__((__nonnull__ (1, 2)));
extern exit_code call_write (const char *wbuf, size_t wbuflen,
                             const char **argv)
  __attribute__((__nonnull__ (1, 3)));

#endif /* NBDKIT_CALL_H */
