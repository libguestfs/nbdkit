/* nbdkit
 * Copyright (C) 2019-2020 Red Hat Inc.
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
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>

#include <pthread.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "poll.h"
#include "utils.h"
#include "vector.h"

static unsigned pollsecs = 60;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned connections = 0;
static bool exiting = false;

/* The list of events generated from command line parameters. */
struct event {
  int type;
#define EVENT_FILE_CREATED  1
#define EVENT_FILE_DELETED  2
#ifndef WIN32
#define EVENT_PROCESS_EXITS 3
#define EVENT_FD_CLOSED     4
#define EVENT_SCRIPT        5
#endif
  union {
    char *filename;             /* Filename or script. */
    int fd;                     /* For PROCESS_EXITS or FD_CLOSED. */
#ifndef __linux__
    pid_t pid;                  /* For PROCESS_EXITS on non-Linux. */
#endif
  } u;
};
DEFINE_VECTOR_TYPE(event_list, struct event);
static event_list events = empty_vector;

static void
free_event (struct event event)
{
  switch (event.type) {
#ifdef EVENT_FILE_CREATED
  case EVENT_FILE_CREATED:
#endif
#ifdef EVENT_FILE_DELETED
  case EVENT_FILE_DELETED:
#endif
#ifdef EVENT_SCRIPT
  case EVENT_SCRIPT:
#endif
    free (event.u.filename);
    break;
#ifdef EVENT_PROCESS_EXITS
  case EVENT_PROCESS_EXITS:
#endif
#ifdef EVENT_FD_CLOSED
  case EVENT_FD_CLOSED:
#endif
#ifdef __linux__
    close (event.u.fd);
#endif
    break;
  }
}

static void
exitwhen_unload (void)
{
  event_list_iter (&events, free_event);
  free (events.ptr);
}

/* If exiting is already true, this does nothing and returns true.
 * Otherwise it checks if any event in the list has happened.  If an
 * event has happened, sets exiting to true.  It returns the exiting
 * flag.
 *
 * This must be called with &lock held.
 */
#ifdef EVENT_FILE_CREATED
static void check_for_event_file_created (const struct event *);
#endif
#ifdef EVENT_FILE_DELETED
static void check_for_event_file_deleted (const struct event *);
#endif
#ifdef EVENT_PROCESS_EXITS
static void check_for_event_process_exits (const struct event *);
#endif
#ifdef EVENT_FD_CLOSED
static void check_for_event_fd_closed (const struct event *);
#endif
#ifdef EVENT_SCRIPT
static void check_for_event_script (const struct event *);
#endif

static bool
check_for_event (void)
{
  size_t i;

  if (!exiting) {
    for (i = 0; i < events.len; ++i) {
      const struct event *event = &events.ptr[i];

      switch (event->type) {
#ifdef EVENT_FILE_CREATED
      case EVENT_FILE_CREATED:
        check_for_event_file_created (event);
        break;
#endif
#ifdef EVENT_FILE_DELETED
      case EVENT_FILE_DELETED:
        check_for_event_file_deleted (event);
        break;
#endif
#ifdef EVENT_PROCESS_EXITS
      case EVENT_PROCESS_EXITS:
        check_for_event_process_exits (event);
        break;
#endif
#ifdef EVENT_FD_CLOSED
      case EVENT_FD_CLOSED:
        check_for_event_fd_closed (event);
        break;
#endif
#ifdef EVENT_SCRIPT
      case EVENT_SCRIPT:
        check_for_event_script (event);
        break;
#endif
      }
    }
  }

  return exiting;
}

#ifdef EVENT_FILE_CREATED
static void
check_for_event_file_created (const struct event *event)
{
  if (access (event->u.filename, R_OK) == 0) {
    nbdkit_debug ("exit-when-file-created: detected %s created",
                  event->u.filename);
    exiting = true;
  }
}
#endif

#ifdef EVENT_FILE_DELETED
static void
check_for_event_file_deleted (const struct event *event)
{
  if (access (event->u.filename, R_OK) == -1) {
    if (errno == ENOTDIR || errno == ENOENT) {
      nbdkit_debug ("exit-when-file-deleted: detected %s deleted",
                    event->u.filename);
      exiting = true;
    }
    else {
      /* Log the error but continue. */
      nbdkit_error ("exit-when-file-deleted: access: %s: %m",
                    event->u.filename);
    }
  }
}
#endif

#ifdef EVENT_PROCESS_EXITS
static void
check_for_event_process_exits (const struct event *event)
{
  char c;

#ifdef __linux__
  /* https://gitlab.freedesktop.org/polkit/polkit/-/issues/75
   *
   * event->u.fd holds /proc/PID/stat of the original process open.
   * If we can still read a byte from it then the original process is
   * still around.  If we get ESRCH then the process has exited.
   */
  lseek (event->u.fd, 0, SEEK_SET);
  if (read (event->u.fd, &c, 1) == -1) {
    if (errno == ESRCH) {
      nbdkit_debug ("exit-when-process-exits: detected process exit");
      exiting = true;
    }
    else {
      /* Log the error but continue. */
      nbdkit_error ("exit-when-process-exits: read: %m");
    }
  }
#else /* !__linux__ */
  /* XXX Find a safe way to do this on BSD at least. */
  if (kill (event->u.pid, 0) == -1 && errno == ESRCH) {
    nbdkit_debug ("exit-when-process-exits: detected process exit");
    exiting = true;
  }
#endif /* !__linux__ */
}
#endif

#ifdef EVENT_FD_CLOSED
static void
check_for_event_fd_closed (const struct event *event)
{
  int r;
  struct pollfd fds[1];

  /* event->u.fd is the read side of a pipe or socket.  Check it is
   * not closed.  We don't actually read anything from the pipe.
   */
  fds[0].fd = event->u.fd;
  fds[0].events = 0;
  r = poll (fds, 1, 0);
  if (r == 1) {
    if ((fds[0].revents & POLLHUP) != 0) {
      nbdkit_debug ("exit-when-pipe-closed: detected pipe closed");
      exiting = true;
    }
    else if ((fds[0].revents & POLLNVAL) != 0) {
      /* If we were passed a bad file descriptor that is user error
       * and we should exit with an error early.  Because
       * check_for_event() is called first in get_ready() this should
       * cause this to happen.
       */
      nbdkit_error ("exit-when-pipe-closed: invalid file descriptor");
      exiting = true;
    }
  }
  else if (r == -1) {
    /* Log the error but continue. */
    nbdkit_error ("exit-when-pipe-closed: poll: %m");
  }
}
#endif

#ifdef EVENT_SCRIPT
static void
check_for_event_script (const struct event *event)
{
  int r;

  /* event->u.filename is a script filename or command.  Exit code 88
   * indicates the event has happened.
   */
  r = system (event->u.filename);
  if (r == -1) {
    /* Log the error but continue. */
    nbdkit_error ("exit-when-script: %m");
  }
  else if (WIFEXITED (r) && WEXITSTATUS (r) == 0) {
    /* Normal case, do nothing. */
  }
  else if (WIFEXITED (r) && WEXITSTATUS (r) == 88) {
    nbdkit_debug ("exit-when-script: detected scripted event");
    exiting = true;
  }
  else {
    /* Log the error but continue. */
    exit_status_to_nbd_error (r, "exit-when-script");
  }
}
#endif

/* The background polling thread.
 *
 * This runs continuously in the background, but you can pause it by
 * grabbing &pause_lock (use the pause/resume_polling_thread()
 * wrappers below).
 */
static pthread_mutex_t pause_lock = PTHREAD_MUTEX_INITIALIZER;

static void *
polling_thread (void *vp)
{
  for (;;) {
    {
      /* Note the order here is chosen to avoid possible deadlock
       * because the caller of pause_polling_thread() always acquires
       * &lock before &pause_lock.
       */
      ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
      ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&pause_lock);
      if (check_for_event ()) {
        nbdkit_debug ("exitwhen: shutdown from polling thread");
        nbdkit_shutdown ();
      }
    }

    sleep (pollsecs);
  }
}

/* Call this to pause the polling thread.  &lock must be held. */
static void
pause_polling_thread (void)
{
  pthread_mutex_lock (&pause_lock);
}

/* Call this to resume the polling thread. */
static void
resume_polling_thread (void)
{
  pthread_mutex_unlock (&pause_lock);
}

/* Read command line parameters are build events list. */
static int
exitwhen_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
                 const char *key, const char *value)
{
  struct event event;

  if (0) ;
#if defined(EVENT_FILE_CREATED) && defined(EVENT_FILE_DELETED)
  else if (strcmp (key, "exit-when-file-created") == 0 ||
      strcmp (key, "exit-when-file-deleted") == 0) {
    char c = key[15];

    assert (c == 'c' || c == 'd');
    event.type = c == 'c' ? EVENT_FILE_CREATED : EVENT_FILE_DELETED;
    event.u.filename = nbdkit_absolute_path (value);
    if (event.u.filename == NULL)
      return -1;
  append_event:
    if (event_list_append (&events, event) == -1)
      return -1;
    return 0;
  }
#endif
#ifdef EVENT_FD_CLOSED
  else if (strcmp (key, "exit-when-pipe-closed") == 0 ||
           strcmp (key, "exit-when-fd-closed") == 0) {
    event.type = EVENT_FD_CLOSED;
    if (nbdkit_parse_int ("exit-when-pipe-closed", value, &event.u.fd) == -1)
      return -1;
    goto append_event;
  }
#endif
#ifdef EVENT_PROCESS_EXITS
  else if (strcmp (key, "exit-when-process-exits") == 0 ||
           strcmp (key, "exit-when-pid-exits") == 0) {
    uint64_t pid;
    CLEANUP_FREE char *str = NULL;

    event.type = EVENT_PROCESS_EXITS;
    if (nbdkit_parse_uint64_t ("exit-when-process-exits", value, &pid) == -1)
      return -1;
#ifdef __linux__
    /* See: https://gitlab.freedesktop.org/polkit/polkit/-/issues/75 */
    if (asprintf (&str, "/proc/%" PRIu64 "/stat", pid) == -1) {
      nbdkit_error ("asprintf: %m");
      return -1;
    }
    event.u.fd = open (str, O_RDONLY);
    if (event.u.fd == -1) {
      nbdkit_error ("exit-when-process-exits: %s: %m", str);
      return -1;
    }
#else
    event.u.pid = (pid_t) pid;
#endif
    goto append_event;
  }
#endif
#ifdef EVENT_SCRIPT
  else if (strcmp (key, "exit-when-script") == 0) {
    event.type = EVENT_SCRIPT;
    event.u.filename = strdup (value);
    if (event.u.filename == NULL) {
      nbdkit_error ("strdup: %m");
      return -1;
    }
    goto append_event;
  }
#endif

  /* This is a setting, not an event. */
  if (strcmp (key, "exit-when-poll") == 0) {
    if (nbdkit_parse_unsigned ("exit-when-poll", value, &pollsecs) == -1)
      return -1;
    return 0;
  }

  /* Otherwise pass the parameter to the plugin. */
  return next (nxdata, key, value);
}

/* Before forking, run the check.  If the event has already happened
 * then we exit immediately.
 */
static int
exitwhen_get_ready (int thread_model)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);

  if (check_for_event ())
    exit (EXIT_SUCCESS);

  return 0;
}

/* After forking, start the background thread.  Initially it is
 * polling.
 */
static int
exitwhen_after_fork (nbdkit_backend *nxdata)
{
  int err;
  pthread_t thread;

  err = pthread_create (&thread, NULL, polling_thread, NULL);
  if (err != 0) {
    errno = err;
    nbdkit_error ("pthread_create: %m");
    return -1;
  }
  return 0;
}

static int
exitwhen_preconnect (nbdkit_next_preconnect *next, nbdkit_backend *nxdata,
                     int readonly)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);

  if (check_for_event ()) {
    nbdkit_error ("exitwhen: nbdkit is exiting: rejecting new connection");
    return -1;
  }

  if (next (nxdata, readonly) == -1)
    return -1;

  return 0;
}

static void *
exitwhen_open (nbdkit_next_open *next, nbdkit_context *nxdata,
               int readonly, const char *exportname, int is_tls)
{
  if (next (nxdata, readonly, exportname) == -1)
    return NULL;

  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);
  connections++;
  if (connections == 1)
    pause_polling_thread ();

  return NBDKIT_HANDLE_NOT_NEEDED;
}

static void
exitwhen_close (void *handle)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);

  check_for_event ();

  --connections;
  if (connections == 0) {
    if (exiting) {
      nbdkit_debug ("exitwhen: exiting on last client connection");
      nbdkit_shutdown ();
    }
    else
      resume_polling_thread ();
  }
}

static struct nbdkit_filter filter = {
  .name              = "exitwhen",
  .longname          = "nbdkit exitwhen filter",
  .unload            = exitwhen_unload,

  .config            = exitwhen_config,
  .get_ready         = exitwhen_get_ready,
  .after_fork        = exitwhen_after_fork,

  .preconnect        = exitwhen_preconnect,
  .open              = exitwhen_open,
  .close             = exitwhen_close,
};

NBDKIT_REGISTER_FILTER(filter)
