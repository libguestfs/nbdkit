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

/* Implement the --exit-with-parent feature on operating systems which
 * support it.
 */

#include <config.h>

#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#ifdef HAVE_SYS_PROCCTL_H
#include <sys/procctl.h>
#endif

#if defined (HAVE_SYS_PRCTL_H) && defined (PR_SET_PDEATHSIG)

/* For Linux >= 2.1.57. */

int
set_exit_with_parent (void)
{
  return prctl (PR_SET_PDEATHSIG, SIGTERM);
}

bool
can_exit_with_parent (void)
{
  return true;
}

#elif defined (HAVE_SYS_PROCCTL_H) && defined (PROC_PDEATHSIG_CTL)

/* For FreeBSD >= 11.2 */

int
set_exit_with_parent (void)
{
  const int sig = SIGTERM;
  return procctl (P_PID, 0, PROC_PDEATHSIG_CTL, (void*) &sig);
}

bool
can_exit_with_parent (void)
{
  return true;
}

#elif defined (__APPLE__)

/* For macOS. */

#include <unistd.h>
#include <errno.h>
#include <sys/event.h>
#include <pthread.h>

#include "nbdkit-plugin.h"

static void *
exit_with_parent_loop (void *vp)
{
  const pid_t ppid = getppid ();
  int fd;
  struct kevent kev, res[1];
  int r;

  nbdkit_debug ("macOS: --exit-with-parent: "
                "registering exit with parent for ppid %d",
                (int) ppid);

  /* Register the kevent to wait for ppid to exit. */
  fd = kqueue ();
  if (fd == -1) {
    nbdkit_error ("exit_with_parent_loop: kqueue: %m");
    return NULL;
  }
  EV_SET (&kev, ppid, EVFILT_PROC, EV_ADD|EV_ENABLE, NOTE_EXIT, 0, NULL);
  if (kevent (fd, &kev, 1, NULL, 0, NULL) == -1) {
    nbdkit_error ("exit_with_parent_loop: kevent: %m");
    close (fd);
    return NULL;
  }

  /* Wait for the kevent to happen. */
  r = kevent (fd, 0, 0, res, 1, NULL);
  if (r == 1 && res[0].ident == ppid) {
    /* Shut down the whole process when the parent dies. */
    nbdkit_debug ("macOS: --exit-with-parent: "
                  "exit because parent process died");
    nbdkit_shutdown ();
  }

  return NULL;
}

int
set_exit_with_parent (void)
{
  int r;
  pthread_attr_t attrs;
  pthread_t exit_with_parent_thread;

  /* We have to block waiting for kevent, so that requires that we
   * start a background thread.
   */
  pthread_attr_init (&attrs);
  pthread_attr_setdetachstate (&attrs, PTHREAD_CREATE_DETACHED);
  r = pthread_create (&exit_with_parent_thread, NULL,
                      exit_with_parent_loop, NULL);
  if (r != 0) {
    errno = r;
    return -1;
  }

  return 0;
}

bool
can_exit_with_parent (void)
{
  return true;
}

#else /* any platform that doesn't support this function */

int
set_exit_with_parent (void)
{
  abort ();
}

bool
can_exit_with_parent (void)
{
  return false;
}

#endif
