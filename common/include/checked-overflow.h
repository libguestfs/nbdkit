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

/* This header file defines macros and functions for checking overflow in
 * common integer arithmetic operations.
 *
 * The macros use:
 * - the "statement expression" GCC extension,
 * - the "typeof" GCC extension,
 * - the __builtin_add_overflow() and __builtin_mul_overflow() GCC/Clang
 *   built-ins.
 *
 * If either built-in is unavailable, the corresponding macro replaces it with
 * a call to an inline C function.
 */

#ifndef NBDKIT_CHECKED_OVERFLOW_H
#define NBDKIT_CHECKED_OVERFLOW_H

#if !defined (__GNUC__) && !defined (__clang__)
#error "this file may need to be ported to your compiler"
#endif

#include <stdbool.h>
#include <stdint.h>

#include "static-assert.h"
#include "unique-name.h"

/* Add "a" and "b", both of (possibly different) unsigned integer types, and
 * store the sum in "*r", which must also have some unsigned integer type.
 *
 * Each macro argument is evaluated exactly once, as long as it does not have
 * variably modified type.
 *
 * The macro evaluates to "false" if "*r" can represent the mathematical sum.
 * Otherwise, the macro evaluates to "true", and the low order bits of the
 * mathematical sum are stored to "*r".
 */
#if HAVE_DECL___BUILTIN_ADD_OVERFLOW
#define ADD_OVERFLOW(a, b, r) ADD_OVERFLOW_BUILTIN ((a), (b), (r))
#else
#define ADD_OVERFLOW(a, b, r) ADD_OVERFLOW_FALLBACK ((a), (b), (r))
#endif

/* Multiply "a" and "b", both of (possibly different) unsigned integer types,
 * and store the product in "*r", which must also have some unsigned integer
 * type.
 *
 * Each macro argument is evaluated exactly once, as long as it does not have
 * variably modified type.
 *
 * The macro evaluates to "false" if "*r" can represent the mathematical
 * product. Otherwise, the macro evaluates to "true", and the low order bits of
 * the mathematical product are stored to "*r".
 */
#if HAVE_DECL___BUILTIN_MUL_OVERFLOW
#define MUL_OVERFLOW(a, b, r) MUL_OVERFLOW_BUILTIN ((a), (b), (r))
#else
#define MUL_OVERFLOW(a, b, r) MUL_OVERFLOW_FALLBACK ((a), (b), (r))
#endif

/* The ADD_OVERFLOW_BUILTIN and MUL_OVERFLOW_BUILTIN function-like macros
 * enforce the unsignedness of all their operands even though the underlying
 * compiler built-ins, __builtin_add_overflow() and __builtin_mul_overflow(),
 * don't depend on that. The explanation is that the fallback implementation
 * does depend on the unsignedness of all operands, and the codebase should
 * seamlessly build regardless of the built-in vs. fallback choice.
 *
 * Each macro argument is evaluated exactly once, as long as it does not have
 * variably modified type.
 */
#if HAVE_DECL___BUILTIN_ADD_OVERFLOW
#define ADD_OVERFLOW_BUILTIN(a, b, r)       \
  ({                                        \
    STATIC_ASSERT_UNSIGNED_INT (a);         \
    STATIC_ASSERT_UNSIGNED_INT (b);         \
    STATIC_ASSERT_UNSIGNED_INT (*(r));      \
    __builtin_add_overflow ((a), (b), (r)); \
  })
#endif

#if HAVE_DECL___BUILTIN_MUL_OVERFLOW
#define MUL_OVERFLOW_BUILTIN(a, b, r)       \
  ({                                        \
    STATIC_ASSERT_UNSIGNED_INT (a);         \
    STATIC_ASSERT_UNSIGNED_INT (b);         \
    STATIC_ASSERT_UNSIGNED_INT (*(r));      \
    __builtin_mul_overflow ((a), (b), (r)); \
  })
#endif

/* The fallback macros call inline C functions. The unsignedness of all
 * operands is enforced in order to keep the conversion to uintmax_t
 * value-preserving, and to keep the conversion back from uintmax_t independent
 * of the C language implementation.
 *
 * Each macro argument is evaluated exactly once, as long as it does not have
 * variably modified type.
 *
 * The fallback macros and the inline C functions are defined regardless of
 * HAVE_DECL___BUILTIN_ADD_OVERFLOW and HAVE_DECL___BUILTIN_MUL_OVERFLOW so
 * that the test suite can always test the fallback.
 */
#define ADD_OVERFLOW_FALLBACK(a, b, r)                                \
  ADD_OVERFLOW_FALLBACK_1 ((a), (b), (r),                             \
                           NBDKIT_UNIQUE_NAME (_overflow),            \
                           NBDKIT_UNIQUE_NAME (_tmp))
#define ADD_OVERFLOW_FALLBACK_1(a, b, r, overflow, tmp)               \
  ({                                                                  \
    bool overflow;                                                    \
    uintmax_t tmp;                                                    \
                                                                      \
    STATIC_ASSERT_UNSIGNED_INT (a);                                   \
    STATIC_ASSERT_UNSIGNED_INT (b);                                   \
    STATIC_ASSERT_UNSIGNED_INT (*(r));                                \
    overflow = check_add_overflow ((a), (b),                          \
                                   (typeof (*(r)))-1,                 \
                                   &tmp);                             \
    *(r) = tmp;                                                       \
    overflow;                                                         \
  })

#define MUL_OVERFLOW_FALLBACK(a, b, r)                                \
  MUL_OVERFLOW_FALLBACK_1 ((a), (b), (r),                             \
                           NBDKIT_UNIQUE_NAME (_overflow),            \
                           NBDKIT_UNIQUE_NAME (_tmp))
#define MUL_OVERFLOW_FALLBACK_1(a, b, r, overflow, tmp)               \
  ({                                                                  \
    bool overflow;                                                    \
    uintmax_t tmp;                                                    \
                                                                      \
    STATIC_ASSERT_UNSIGNED_INT (a);                                   \
    STATIC_ASSERT_UNSIGNED_INT (b);                                   \
    STATIC_ASSERT_UNSIGNED_INT (*(r));                                \
    overflow = check_mul_overflow ((a), (b),                          \
                                   (typeof (*(r)))-1,                 \
                                   &tmp);                             \
    *(r) = tmp;                                                       \
    overflow;                                                         \
  })

/* Assert, at compile time, that the expression "x" has some unsigned integer
 * type.
 *
 * The expression "x" is not evaluated, unless it has variably modified type.
 */
#define STATIC_ASSERT_UNSIGNED_INT(x) \
  STATIC_ASSERT ((typeof (x))-1 > 0, _x_has_uint_type)

/* Assign the sum "a + b" to "*r", using uintmax_t modular arithmetic.
 *
 * Return true iff the addition overflows or the result exceeds "max".
 */
static inline bool
check_add_overflow (uintmax_t a, uintmax_t b, uintmax_t max, uintmax_t *r)
{
  bool in_range;

  *r = a + b;
  in_range = a <= UINTMAX_MAX - b && *r <= max;
  return !in_range;
}

/* Assign the product "a * b" to "*r", using uintmax_t modular arithmetic.
 *
 * Return true iff the multiplication overflows or the result exceeds "max".
 */
static inline bool
check_mul_overflow (uintmax_t a, uintmax_t b, uintmax_t max, uintmax_t *r)
{
  bool in_range;

  *r = a * b;
  in_range = b == 0 || (a <= UINTMAX_MAX / b && *r <= max);
  return !in_range;
}

#endif /* NBDKIT_CHECKED_OVERFLOW_H */
