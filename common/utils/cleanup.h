/* nbdkit
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

#ifndef NBDKIT_CLEANUP_H
#define NBDKIT_CLEANUP_H

#include <pthread.h>
#include <assert.h>

/* https://stackoverflow.com/a/1597129 */
#define XXUNIQUE_VAR(name, line) name ## line
#define XUNIQUE_VAR(name, line) XXUNIQUE_VAR (name, line)
#define UNIQUE_VAR(name) XUNIQUE_VAR (name, __LINE__)

/* cleanup.c */
extern void cleanup_free (void *ptr);
#define CLEANUP_FREE __attribute__((cleanup (cleanup_free)))

extern void cleanup_mutex_unlock (pthread_mutex_t **ptr);
#define CLEANUP_MUTEX_UNLOCK __attribute__((cleanup (cleanup_mutex_unlock)))

#define ACQUIRE_LOCK_FOR_CURRENT_SCOPE(mutex)                           \
  CLEANUP_MUTEX_UNLOCK pthread_mutex_t *UNIQUE_VAR(_lock) = mutex;      \
  do {                                                                  \
    int _r = pthread_mutex_lock (UNIQUE_VAR(_lock));                    \
    assert (!_r);                                                       \
  } while (0)

extern void cleanup_rwlock_unlock (pthread_rwlock_t **ptr);
#define CLEANUP_RWLOCK_UNLOCK __attribute__((cleanup (cleanup_rwlock_unlock)))

#define ACQUIRE_WRLOCK_FOR_CURRENT_SCOPE(rwlock)                        \
  CLEANUP_RWLOCK_UNLOCK pthread_rwlock_t *UNIQUE_VAR(_rwlock) = rwlock; \
  do {                                                                  \
    int _r = pthread_rwlock_wrlock (UNIQUE_VAR(_rwlock));               \
    assert (!_r);                                                       \
  } while (0)

#define ACQUIRE_RDLOCK_FOR_CURRENT_SCOPE(rwlock)                        \
  CLEANUP_RWLOCK_UNLOCK pthread_rwlock_t *UNIQUE_VAR(_rwlock) = rwlock; \
  do {                                                                  \
    int _r = pthread_rwlock_rdlock (UNIQUE_VAR(_rwlock));               \
    assert (!_r);                                                       \
  } while (0)

/* cleanup-nbdkit.c */
struct nbdkit_extents;
extern void cleanup_extents_free (struct nbdkit_extents **ptr);
#define CLEANUP_EXTENTS_FREE __attribute__((cleanup (cleanup_extents_free)))

#endif /* NBDKIT_CLEANUP_H */
