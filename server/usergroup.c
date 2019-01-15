/* nbdkit
 * Copyright (C) 2019 Red Hat Inc.
 * All rights reserved.
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
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <sys/types.h>

#include "internal.h"

static uid_t parseuser (const char *);
static gid_t parsegroup (const char *);

/* Handle the -u and -g options.  If user and group are non-NULL
 * then this parses them to work out the UID/GID and changes
 * user and group.
 */
void
change_user (void)
{
  if (group) {
    gid_t gid = parsegroup (group);

    if (setgid (gid) == -1) {
      perror ("setgid");
      exit (EXIT_FAILURE);
    }

    /* Kill supplemental groups from parent process. */
    if (setgroups (1, &gid) == -1) {
      perror ("setgroups");
      exit (EXIT_FAILURE);
    }

    debug ("changed group to %s", group);
  }

  if (user) {
    uid_t uid = parseuser (user);

    if (setuid (uid) == -1) {
      perror ("setuid");
      exit (EXIT_FAILURE);
    }

    debug ("changed user to %s", user);
  }
}

static uid_t
parseuser (const char *id)
{
  struct passwd *pwd;
  int saved_errno;

  errno = 0;
  pwd = getpwnam (id);

  if (NULL == pwd) {
    int val;

    saved_errno = errno;

    if (sscanf (id, "%d", &val) == 1)
      return val;

    fprintf (stderr, "%s: -u option: %s is not a valid user name or uid",
             program_name, id);
    if (saved_errno != 0)
      fprintf (stderr, " (getpwnam error: %s)", strerror (saved_errno));
    fprintf (stderr, "\n");
    exit (EXIT_FAILURE);
  }

  return pwd->pw_uid;
}

static gid_t
parsegroup (const char *id)
{
  struct group *grp;
  int saved_errno;

  errno = 0;
  grp = getgrnam (id);

  if (NULL == grp) {
    int val;

    saved_errno = errno;

    if (sscanf (id, "%d", &val) == 1)
      return val;

    fprintf (stderr, "%s: -g option: %s is not a valid group name or gid",
             program_name, id);
    if (saved_errno != 0)
      fprintf (stderr, " (getgrnam error: %s)", strerror (saved_errno));
    fprintf (stderr, "\n");
    exit (EXIT_FAILURE);
  }

  return grp->gr_gid;
}
