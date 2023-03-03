/* nbdkit
 * Copyright (C) 2013-2022 Red Hat Inc.
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

#define TYPE_IS_ARRAY(a) \
  (!__builtin_types_compatible_p (typeof (a), typeof (&(a)[0])))

#else /* __cplusplus */

#define BUILD_BUG_UNLESS_TRUE(cond) 0
#define TYPE_IS_ARRAY(a) 1

#endif /* __cplusplus */

#endif /* NBDKIT_COMPILER_MACROS_H */
