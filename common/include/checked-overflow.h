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

/* This header file defines functions for checking overflow in common
 * integer arithmetic operations.
 *
 * It uses GCC/Clang built-ins: a possible future enhancement is to
 * provide fallbacks in plain C or for other compilers.  The only
 * purpose of having a header file for this is to have a single place
 * where we would extend this in future.
 */

#ifndef NBDKIT_CHECKED_OVERFLOW_H
#define NBDKIT_CHECKED_OVERFLOW_H

#if !defined(__GNUC__) && !defined(__clang__)
#error "this file may need to be ported to your compiler"
#endif

/* Add two values.  *r = a + b
 * Returns true if overflow happened.
 */
#define ADD_OVERFLOW(a, b, r) __builtin_add_overflow((a), (b), (r))

/* Multiply two values.  *r = a * b
 * Returns true if overflow happened.
 */
#define MUL_OVERFLOW(a, b, r) __builtin_mul_overflow((a), (b), (r))

#endif /* NBDKIT_CHECKED_OVERFLOW_H */
