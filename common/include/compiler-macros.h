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

#ifndef NBDKIT_COMPILER_MACROS_H
#define NBDKIT_COMPILER_MACROS_H

#ifndef __cplusplus

/* BUILD_BUG_UNLESS_TRUE(1) => 0
 * BUILD_BUG_UNLESS_TRUE(0) => compile-time failure
 *
 * It works by constructing a struct which has an impossible
 * (negative-sized) bitfield in the false case.  Anonymous bitfields
 * are permitted in C99 and above.
 *
 * The Linux kernel calls this BUILD_BUG_ON_ZERO(!cond) which is a
 * confusing name.  It has the same semantics except cond is negated.
 */
#define BUILD_BUG_STRUCT_SIZE(cond) \
  (sizeof (struct { int: (cond) ? 1 : -1; }))
#define BUILD_BUG_UNLESS_TRUE(cond) \
  (BUILD_BUG_STRUCT_SIZE (cond) - BUILD_BUG_STRUCT_SIZE (cond))

/* Each of TYPE_IS_POINTER() and TYPE_IS_ARRAY() produces a build failure if it
 * is invoked with an object that has neither pointer-to-object type nor array
 * type.
 *
 * C99 6.5.2.1 constrains one of the operands of the subscript operator to have
 * pointer-to-object type, and the other operand to have integer type. In the
 * replacement text of TYPE_IS_POINTER(), we use [0] as subscript (providing the
 * integer operand), therefore the macro argument (p) is constrained to have
 * pointer-to-object type.
 *
 * If TYPE_IS_POINTER() is invoked with a pointer that has pointer-to-object
 * type, the constraint is directly satisfied, and TYPE_IS_POINTER() evaluates,
 * at compile time, to 1.
 *
 * If TYPE_IS_POINTER() is invoked with an array, the constraint of the
 * subscript operator is satisfied again -- because the array argument "decays"
 * to a pointer to the array's initial element (C99 6.3.2p3) --, and
 * TYPE_IS_POINTER() evaluates, at compile time, to 0.
 *
 * If TYPE_IS_POINTER() is invoked with an argument having any other type, then
 * the subscript operator constraint is not satisfied, and C99 5.1.1.3p1
 * requires the emission of a diagnostic message -- the build breaks. Therefore,
 * TYPE_IS_ARRAY() can be defined simply as the logical negation of
 * TYPE_IS_POINTER().
 */
#define TYPE_IS_POINTER(p) \
  (__builtin_types_compatible_p (typeof (p), typeof (&(p)[0])))
#define TYPE_IS_ARRAY(a) (!TYPE_IS_POINTER (a))

#else /* __cplusplus */

#define BUILD_BUG_UNLESS_TRUE(cond) 0
#define TYPE_IS_POINTER(p) 1
#define TYPE_IS_ARRAY(a) 1

#endif /* __cplusplus */

#endif /* NBDKIT_COMPILER_MACROS_H */
