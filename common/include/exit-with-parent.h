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

/* Implement the --exit-with-parent feature on operating systems which
 * support it.
 */

#ifndef NBDKIT_EXIT_WITH_PARENT_H
#define NBDKIT_EXIT_WITH_PARENT_H

#include <config.h>

#include <signal.h>

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#ifdef HAVE_SYS_PROCCTL_H
#include <sys/procctl.h>
#endif

#if defined(HAVE_SYS_PRCTL_H) && defined(PR_SET_PDEATHSIG)

/* For Linux >= 2.1.57. */

static inline int
set_exit_with_parent (void)
{
  return prctl (PR_SET_PDEATHSIG, SIGTERM);
}

#define HAVE_EXIT_WITH_PARENT 1

#elif defined(HAVE_SYS_PROCCTL_H) && defined(PROC_PDEATHSIG_CTL)

/* For FreeBSD >= 11.2 */

static inline int
set_exit_with_parent (void)
{
  const int sig = SIGTERM;
  return procctl (P_PID, 0, PROC_PDEATHSIG_CTL, (void*) &sig);
}

#define HAVE_EXIT_WITH_PARENT 1

#endif

#endif /* NBDKIT_INTERNAL_H */
