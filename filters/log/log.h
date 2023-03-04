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

#ifndef NBDKIT_LOG_H
#define NBDKIT_LOG_H

#include <stdint.h>
#include <stdarg.h>

#include <pthread.h>

#include <nbdkit-filter.h>

typedef uint64_t log_id_t;

struct handle {
  uint64_t connection;
  log_id_t id;
  const char *exportname;
  int tls;
};

extern uint64_t connections;
extern const char *logfilename;
extern FILE *logfile;
extern const char *logscript;
extern int append;
extern pthread_mutex_t lock;

/* Compute the next id number on the current connection. */
static inline log_id_t
get_id (struct handle *h)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
  return ++h->id;
}

/* enter() and leave() are called on entry and exit to every filter
 * method and handle the logging.
 *
 * Some methods (like .prepare) only print() a single mssage.
 */
extern void enter (struct handle *h, log_id_t id, const char *act,
                   const char *fmt, ...)
  ATTRIBUTE_FORMAT_PRINTF (4, 5);
extern void leave (struct handle *h, log_id_t id, const char *act,
                   const char *fmt, ...)
  ATTRIBUTE_FORMAT_PRINTF (4, 5);
extern void print (struct handle *h, const char *act,
                   const char *fmt, ...)
  ATTRIBUTE_FORMAT_PRINTF (3, 4);

/* In the case where leave() only has to print result-or-error, this
 * simplified version is used instead.
 */
extern void leave_simple (struct handle *h, log_id_t id, const char *act,
                          int r, int *err);

/* For simple methods, define a macro which automatically calls
 * enter() on entry, and leave_simple() on each exit path.
 */
struct leave_simple_params {
  struct handle *h;
  log_id_t id;
  const char *act;
  int *r;
  int *err;
};

extern void leave_simple2 (struct leave_simple_params *params);

#define LOG(h, act, r, err, ...)                                        \
  log_id_t id = get_id (h);                                             \
  __attribute__ ((cleanup (leave_simple2)))                             \
  CLANG_UNUSED_VARIABLE_WORKAROUND                                      \
  struct leave_simple_params _params = { h, id, act, &r, err };         \
  enter ((h), id, (act), ##__VA_ARGS__)

#endif /* NBDKIT_LOG_H */
