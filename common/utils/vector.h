/* nbdkit
 * Copyright (C) 2020 Red Hat Inc.
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

/* Simple implementation of a vector.  It can be cheaply appended, and
 * more expensively inserted.  There are two main use-cases we
 * consider: lists of strings (either with a defined length, or
 * NULL-terminated), and lists of numbers.  It is generic so could be
 * used for lists of anything (eg. structs) where being able to append
 * easily is important.
 */

#ifndef NBDKIT_VECTOR_H
#define NBDKIT_VECTOR_H

#include <assert.h>
#include <string.h>

#define DEFINE_VECTOR_TYPE(name, type)                                  \
  struct name {                                                         \
    type *ptr;                 /* Pointer to array of items. */         \
    size_t size;               /* Number of valid items in the array. */ \
    size_t alloc;              /* Number of items allocated. */         \
  };                                                                    \
  typedef struct name name;                                             \
  static inline int                                                     \
  name##_reserve (name *v, size_t n)                                    \
  {                                                                     \
    return generic_vector_reserve ((struct generic_vector *)v, n,       \
                                   sizeof (type));                      \
  }                                                                     \
  /* Insert at i'th element.  i=0 => beginning  i=size => append */     \
  static inline int                                                     \
  name##_insert (name *v, type elem, size_t i)                          \
  {                                                                     \
    if (v->size >= v->alloc) {                                          \
      if (name##_reserve (v, 1) == -1) return -1;                       \
    }                                                                   \
    memmove (&v->ptr[i+1], &v->ptr[i], (v->size-i) * sizeof (elem));    \
    v->ptr[i] = elem;                                                   \
    v->size++;                                                          \
    return 0;                                                           \
  }                                                                     \
  static inline int                                                     \
  name##_append (name *v, type elem)                                    \
  {                                                                     \
    return name##_insert (v, elem, v->size);                            \
  }                                                                     \
  static inline void                                                    \
  name##_iter (name *v, void (*f) (type elem))                          \
  {                                                                     \
    size_t i;                                                           \
    for (i = 0; i < v->size; ++i)                                       \
      f (v->ptr[i]);                                                    \
  }

#define empty_vector { .ptr = NULL, .size = 0, .alloc = 0 }

struct generic_vector {
  void *ptr;
  size_t size;
  size_t alloc;
};

extern int generic_vector_reserve (struct generic_vector *v,
                                   size_t n, size_t itemsize);

#endif /* NBDKIT_VECTOR_H */
