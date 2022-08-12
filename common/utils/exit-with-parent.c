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

#if defined(HAVE_SYS_PRCTL_H) && defined(PR_SET_PDEATHSIG)

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

#elif defined(HAVE_SYS_PROCCTL_H) && defined(PROC_PDEATHSIG_CTL)

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

#elif defined(__APPLE__)

/* For macOS.
 *
 * Adapted from:
 * https://developer.apple.com/documentation/corefoundation/cffiledescriptor-ru3
 * and https://stackoverflow.com/a/6484903 (note this example is
 * wrong).
 */

#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>
#include <signal.h>
#include <sys/event.h>

#include "nbdkit-plugin.h"

static pid_t nbdkit_pid = 0;

static void
parent_died (CFFileDescriptorRef fdref, CFOptionFlags callBackTypes,
             void *info)
{
  if (nbdkit_pid > 0) {
    nbdkit_debug ("macOS: --exit-with-parent: "
                  "kill nbdkit (pid %d) because parent process died",
                  nbdkit_pid);
    kill (nbdkit_pid, SIGTERM);
    nbdkit_pid = 0;
    exit (EXIT_SUCCESS);
  }
}

static void
do_monitor (pid_t pid)
{
  int fd;
  struct kevent kev;
  CFFileDescriptorRef fdref;
  CFRunLoopSourceRef source;

  fd = kqueue ();
  if (fd == -1)
    _exit (EXIT_FAILURE);
  EV_SET (&kev, pid, EVFILT_PROC, EV_ADD|EV_ENABLE, NOTE_EXIT, 0, NULL);
  if (kevent (fd, &kev, 1, NULL, 0, NULL) == -1)
    _exit (EXIT_FAILURE);

  fdref = CFFileDescriptorCreate (kCFAllocatorDefault, fd, true,
                                  parent_died, NULL);
  if (fdref == NULL)
    _exit (EXIT_FAILURE);
  CFFileDescriptorEnableCallBacks (fdref, kCFFileDescriptorReadCallBack);
  source =
    CFFileDescriptorCreateRunLoopSource (kCFAllocatorDefault, fdref, 0);
  if (source == NULL)
    _exit (EXIT_FAILURE);
  CFRunLoopAddSource (CFRunLoopGetMain(), source, kCFRunLoopDefaultMode);
  CFRelease (source);
}

static void
exit_monitor_process (CFFileDescriptorRef fdref, CFOptionFlags callBackTypes,
                      void *info)
{
  nbdkit_debug ("macOS: --exit-with-parent: "
                "monitor exiting because nbdkit exited");
  exit (EXIT_SUCCESS);
}

static void
do_exit_for_nbdkit (pid_t pid)
{
  int fd;
  struct kevent kev;
  CFFileDescriptorRef fdref;
  CFRunLoopSourceRef source;

  fd = kqueue ();
  if (fd == -1)
    _exit (EXIT_FAILURE);
  EV_SET (&kev, pid, EVFILT_PROC, EV_ADD|EV_ENABLE, NOTE_EXIT, 0, NULL);
  if (kevent (fd, &kev, 1, NULL, 0, NULL) == -1)
    _exit (EXIT_FAILURE);

  fdref = CFFileDescriptorCreate (kCFAllocatorDefault, fd, true,
                                  exit_monitor_process, NULL);
  if (fdref == NULL)
    _exit (EXIT_FAILURE);
  CFFileDescriptorEnableCallBacks (fdref, kCFFileDescriptorReadCallBack);
  source =
    CFFileDescriptorCreateRunLoopSource (kCFAllocatorDefault, fdref, 0);
  if (source == NULL)
    _exit (EXIT_FAILURE);
  CFRunLoopAddSource (CFRunLoopGetMain(), source, kCFRunLoopDefaultMode);
  CFRelease (source);
}

int
set_exit_with_parent (void)
{
  pid_t ppid = getppid ();
  pid_t monitor_pid;

  nbdkit_debug ("macOS: --exit-with-parent: "
                "registering exit with parent for ppid %d",
                (int) ppid);
  nbdkit_pid = getpid ();

  /* We have to run a main loop (ie a new process) to get similar
   * behaviour to --exit-with-parent on other platforms.
   *
   * nbdkit_pid = nbdkit's PID
   * ppid = parent of nbdkit that we are monitoring
   * monitor_pid = monitoring PID
   */
  monitor_pid = fork ();
  if (monitor_pid == 0) {       /* Child (monitoring process) */
    do_monitor (ppid);          /* Monitor this parent PID. */
    do_exit_for_nbdkit (nbdkit_pid); /* Just exit if nbdkit exits. */
    CFRunLoopRun ();

    _exit (EXIT_SUCCESS);
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
