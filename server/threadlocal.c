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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>

#include <pthread.h>

#include "internal.h"

/* Note that most thread-local storage data is informational, used for
 * smart error and debug messages on the server side.  However, error
 * tracking can be used to influence which error is sent to the client
 * in a reply.
 *
 * The main thread does not have any associated Thread Local Storage,
 * *unless* it is serving a request (the '-s' option).
 */

struct threadlocal {
  char *name;                   /* Can be NULL. */
  size_t instance_num;          /* Can be 0. */
  int err;
  void *buffer;                 /* Can be NULL. */
  size_t buffer_size;
  struct connection *conn;      /* Can be NULL. */
  struct context *ctx;          /* Can be NULL. */
};

static pthread_key_t threadlocal_key;

static void
free_threadlocal (void *threadlocalv)
{
  struct threadlocal *threadlocal = threadlocalv;

  free (threadlocal->name);
  free (threadlocal->buffer);
  free (threadlocal);
}

void
threadlocal_init (void)
{
  int err;

  err = pthread_key_create (&threadlocal_key, free_threadlocal);
  if (err != 0) {
    fprintf (stderr, "%s: pthread_key_create: %s\n",
             program_name, strerror (err));
    exit (EXIT_FAILURE);
  }
}

void
threadlocal_new_server_thread (void)
{
  struct threadlocal *threadlocal;
  int err;

  threadlocal = calloc (1, sizeof *threadlocal);
  if (threadlocal == NULL) {
    perror ("malloc");
    exit (EXIT_FAILURE);
  }
  err = pthread_setspecific (threadlocal_key, threadlocal);
  if (err) {
    errno = err;
    perror ("pthread_setspecific");
    exit (EXIT_FAILURE);
  }
}

void
threadlocal_set_name (const char *name)
{
  struct threadlocal *threadlocal = pthread_getspecific (threadlocal_key);

  /* Copy name, as the original may be residing in a module, but we
   * want our thread name to persist even after unload. */
  if (threadlocal) {
    free (threadlocal->name);
    threadlocal->name = strdup (name);
    /* Best effort; logging a NULL name is better than exiting. */
    if (threadlocal->name == NULL)
      perror ("malloc");
  }
}

void
threadlocal_set_instance_num (size_t instance_num)
{
  struct threadlocal *threadlocal = pthread_getspecific (threadlocal_key);

  if (threadlocal)
    threadlocal->instance_num = instance_num;
}

const char *
threadlocal_get_name (void)
{
  struct threadlocal *threadlocal = pthread_getspecific (threadlocal_key);

  if (!threadlocal)
    return NULL;

  return threadlocal->name;
}

size_t
threadlocal_get_instance_num (void)
{
  struct threadlocal *threadlocal = pthread_getspecific (threadlocal_key);

  if (!threadlocal)
    return 0;

  return threadlocal->instance_num;
}

void
threadlocal_set_error (int err)
{
  struct threadlocal *threadlocal = pthread_getspecific (threadlocal_key);

  if (threadlocal)
    threadlocal->err = err;
  else
    errno = err;
}

/* This preserves errno, for convenience.
 */
int
threadlocal_get_error (void)
{
  int err = errno;
  struct threadlocal *threadlocal = pthread_getspecific (threadlocal_key);

  errno = err;
  return threadlocal ? threadlocal->err : 0;
}

/* Return the single pread/pwrite buffer for this thread.  The buffer
 * size is increased to ‘size’ bytes if required.
 *
 * The buffer starts out as zeroes but after use may contain data from
 * previous requests.  This is fine because: (a) Correctly written
 * plugins should overwrite the whole buffer on each request so no
 * leak should occur.  (b) The aim of this buffer is to avoid leaking
 * random heap data from the core server; previous request data from
 * the plugin is not considered sensitive.
 */
extern void *
threadlocal_buffer (size_t size)
{
  struct threadlocal *threadlocal = pthread_getspecific (threadlocal_key);

  if (!threadlocal)
    abort ();

  if (threadlocal->buffer_size < size) {
    void *ptr;

    ptr = realloc (threadlocal->buffer, size);
    if (ptr == NULL) {
      nbdkit_error ("threadlocal_buffer: realloc: %m");
      return NULL;
    }
    memset (ptr, 0, size);
    threadlocal->buffer = ptr;
    threadlocal->buffer_size = size;
  }

  return threadlocal->buffer;
}

/* Set (or clear) the connection that is using the current thread */
void
threadlocal_set_conn (struct connection *conn)
{
  struct threadlocal *threadlocal = pthread_getspecific (threadlocal_key);

  if (threadlocal)
    threadlocal->conn = conn;
}

/* Get the connection associated with this thread, if available */
struct connection *
threadlocal_get_conn (void)
{
  struct threadlocal *threadlocal = pthread_getspecific (threadlocal_key);

  return threadlocal ? threadlocal->conn : NULL;
}

/* Set (or clear) the context using the current thread.  This function
 * should generally not be used directly, instead see the macro
 * PUSH_CONTEXT_FOR_SCOPE.
 */
struct context *
threadlocal_push_context (struct context *ctx)
{
  struct threadlocal *threadlocal = pthread_getspecific (threadlocal_key);
  struct context *ret = NULL;

  if (threadlocal) {
    ret = threadlocal->ctx;
    threadlocal->ctx = ctx;
  }
  return ret;
}

void
threadlocal_pop_context (struct context **ctx)
{
  threadlocal_push_context (*ctx);
}
