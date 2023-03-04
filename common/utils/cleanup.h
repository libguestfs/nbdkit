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

#ifndef NBDKIT_CLEANUP_H
#define NBDKIT_CLEANUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <assert.h>

#include "unique-name.h"

/* Work around clang bug: https://bugs.llvm.org/show_bug.cgi?id=43482 */
#ifdef __clang__
#define CLANG_UNUSED_VARIABLE_WORKAROUND __attribute__ ((__unused__))
#else
#define CLANG_UNUSED_VARIABLE_WORKAROUND
#endif

/* cleanup.c */
extern void cleanup_free (void *ptr);
#define CLEANUP_FREE __attribute__ ((cleanup (cleanup_free)))

extern void cleanup_mutex_unlock (pthread_mutex_t **ptr);
#define CLEANUP_MUTEX_UNLOCK __attribute__ ((cleanup (cleanup_mutex_unlock)))

#define ACQUIRE_LOCK_FOR_CURRENT_SCOPE(mutex) \
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE_1 ((mutex), NBDKIT_UNIQUE_NAME (_lock))
#define ACQUIRE_LOCK_FOR_CURRENT_SCOPE_1(mutex, lock)                   \
  CLEANUP_MUTEX_UNLOCK pthread_mutex_t *lock = mutex;                   \
  do {                                                                  \
    int _r = pthread_mutex_lock (lock);                                 \
    assert (!_r);                                                       \
  } while (0)

extern void cleanup_rwlock_unlock (pthread_rwlock_t **ptr);
#define CLEANUP_RWLOCK_UNLOCK __attribute__ ((cleanup (cleanup_rwlock_unlock)))

#define ACQUIRE_WRLOCK_FOR_CURRENT_SCOPE(rwlock) \
  ACQUIRE_WRLOCK_FOR_CURRENT_SCOPE_1 ((rwlock), NBDKIT_UNIQUE_NAME (_lock))
#define ACQUIRE_WRLOCK_FOR_CURRENT_SCOPE_1(rwlock, lock)                \
  CLEANUP_RWLOCK_UNLOCK pthread_rwlock_t *lock = rwlock;                \
  do {                                                                  \
    int _r = pthread_rwlock_wrlock (lock);                              \
    assert (!_r);                                                       \
  } while (0)

#define ACQUIRE_RDLOCK_FOR_CURRENT_SCOPE(rwlock) \
  ACQUIRE_RDLOCK_FOR_CURRENT_SCOPE_1 ((rwlock), NBDKIT_UNIQUE_NAME (_lock))
#define ACQUIRE_RDLOCK_FOR_CURRENT_SCOPE_1(rwlock, lock)                \
  CLEANUP_RWLOCK_UNLOCK pthread_rwlock_t *lock = rwlock;                \
  do {                                                                  \
    int _r = pthread_rwlock_rdlock (lock);                              \
    assert (!_r);                                                       \
  } while (0)

/* cleanup-nbdkit.c */
struct nbdkit_extents;
extern void cleanup_extents_free (struct nbdkit_extents **ptr);
#define CLEANUP_EXTENTS_FREE __attribute__ ((cleanup (cleanup_extents_free)))
struct nbdkit_exports;
extern void cleanup_exports_free (struct nbdkit_exports **ptr);
#define CLEANUP_EXPORTS_FREE __attribute__ ((cleanup (cleanup_exports_free)))

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NBDKIT_CLEANUP_H */
