/* nbdkit
 * Copyright (C) 2017-2022 Red Hat Inc.
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

/* Test file plugin dirfd parameter.  It's not possible to test this
 * using a bash script because bash refuses to open a directory as a
 * file descriptor.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

#include <libnbd.h>

#include "string-vector.h"

#define NBDKIT_START_TIMEOUT 60

static char tmpdir[] = "/tmp/disksXXXXXX";
static char pidpath[] = "/tmp/nbdkitpidXXXXXX";
static char sockpath[] = "/tmp/nbdkitsockXXXXXX";
static pid_t pid = 0;
static void cleanup (void);

string_vector exports;
static int get_export (void *opaque, const char *name, const char *desc);

static int
string_compare (const char **p1, const char **p2)
{
  return strcmp (*p1, *p2);
}

int
main (int argc, char *argv[])
{
  char disk1[64], disk2[64];
  int dfd, fd;
  char dirfd_eq_dfd[64];
  size_t i;
  struct nbd_handle *nbd;
  char wdata[256];
  char rdata[256];

  if (system ("nbdkit --exit-with-parent --version") != 0) {
    printf ("%s: --exit-with-parent is not implemented on this platform, "
            "skipping\n",
            argv[0]);
    exit (77);
  }

  /* Make a temporary directory containing two disks. */
  if (mkdtemp (tmpdir) == NULL) {
    perror ("mkdtemp");
    exit (EXIT_FAILURE);
  }

  snprintf (disk1, sizeof disk1, "%s/disk1", tmpdir);
  snprintf (disk2, sizeof disk2, "%s/disk2", tmpdir);

  if ((fd = open (disk1,
                  O_WRONLY|O_TRUNC|O_CREAT|O_CLOEXEC|O_NOCTTY, 0644)) == -1 ||
      ftruncate (fd, 1024*1024) == -1 ||
      close (fd) == -1) {
    perror (disk1);
    exit (EXIT_FAILURE);
  }

  if ((fd = open (disk2,
                  O_WRONLY|O_TRUNC|O_CREAT|O_CLOEXEC|O_NOCTTY, 0644)) == -1 ||
      ftruncate (fd, 64*1024) == -1 ||
      close (fd) == -1) {
    perror (disk2);
    exit (EXIT_FAILURE);
  }

  /* Create random socket and PID filenames. */
  fd = mkstemp (sockpath);
  if (fd == -1) {
    perror ("mkstemp");
    exit (EXIT_FAILURE);
  }
  close (fd);
  unlink (sockpath);

  fd = mkstemp (pidpath);
  if (fd == -1) {
    perror ("mkstemp");
    exit (EXIT_FAILURE);
  }
  close (fd);
  unlink (pidpath);

  atexit (cleanup);

  /* Run nbdkit on the directory. */
  dfd = open (tmpdir, O_RDONLY | O_DIRECTORY);
  if (dfd == -1) {
    perror (tmpdir);
    exit (EXIT_FAILURE);
  }
  snprintf (dirfd_eq_dfd, sizeof dirfd_eq_dfd, "dirfd=%d", dfd);

  pid = fork ();
  if (pid == -1) {
    perror ("fork");
    exit (EXIT_FAILURE);
  }

  if (pid == 0) {
    /* child nbdkit inheriting dfd */
    const char *args[] = {
      "nbdkit", "-U", sockpath, "-P", pidpath, "-f", "--exit-with-parent",
      "file", dirfd_eq_dfd,
      NULL
    };

    execvp ("nbdkit", (char **) args);
    perror ("exec: nbdkit");
    _exit (EXIT_FAILURE);
  }

  for (i = 0; i < NBDKIT_START_TIMEOUT; ++i) {
    if (waitpid (pid, NULL, WNOHANG) == pid)
      goto early_exit;

    if (kill (pid, 0) == -1) {
      if (errno == ESRCH) {
      early_exit:
        fprintf (stderr,
                 "FAIL: %s: nbdkit exited before starting to serve files\n",
                 argv[0]);
        exit (EXIT_FAILURE);
      }
      perror ("kill");
    }

    if (access (pidpath, F_OK) == 0)
      break;

    sleep (1);
  }

  /* We should be able to connect to export = "disk1". */
  nbd = nbd_create ();
  if (nbd == NULL) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  nbd_set_export_name (nbd, "disk1");
  if (nbd_connect_unix (nbd, sockpath) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  if (nbd_is_read_only (nbd) != 0) {
    fprintf (stderr, "FAIL: %s: unexpected read only status\n", argv[0]);
    exit (EXIT_FAILURE);
  }

  if (nbd_get_size (nbd) != 1024*1024) {
    fprintf (stderr, "FAIL: %s: unexpected size\n", argv[0]);
    exit (EXIT_FAILURE);
  }

  memset (wdata, 'x', sizeof wdata);
  if (nbd_pwrite (nbd, wdata, sizeof wdata, 1024, 0) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  if (nbd_pread (nbd, rdata, sizeof rdata, 1024, 0) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  if (memcmp (wdata, rdata, sizeof wdata) != 0) {
    fprintf (stderr, "FAIL: %s: could not read back written data\n", argv[0]);
    exit (EXIT_FAILURE);
  }

  nbd_close (nbd);

#ifdef LIBNBD_HAVE_NBD_OPT_LIST

  /* List the exports. */
  nbd = nbd_create ();
  if (nbd == NULL) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  nbd_set_opt_mode (nbd, true);

  if (nbd_connect_unix (nbd, sockpath) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  if (nbd_opt_list (nbd,
                    (nbd_list_callback) { .callback = get_export }) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  nbd_close (nbd);

  /* Print the exports. */
  printf ("%zu exports:\n", exports.len);
  for (i = 0; i < exports.len; ++i)
    printf ("\t%s\n", exports.ptr[i]);
  fflush (stdout);

  /* Check the export list collected during the connection. */
  if (exports.len != 2) {
    fprintf (stderr, "FAIL: %s: incorrect number of exports\n", argv[0]);
    exit (EXIT_FAILURE);
  }
  string_vector_sort (&exports, string_compare);
  if (strcmp (exports.ptr[0], "disk1") != 0 ||
      strcmp (exports.ptr[1], "disk2") != 0) {
    fprintf (stderr, "FAIL: %s: incorrect export names", argv[0]);
    exit (EXIT_FAILURE);
  }

  for (i = 0; i < exports.len; ++i)
    free (exports.ptr[i]);
  free (exports.ptr);

#endif /* LIBNBD_HAVE_NBD_OPT_LIST */

  exit (EXIT_SUCCESS);
}

static void
cleanup (void)
{
  char cmd[256];

  if (pid > 0)
    kill (pid, SIGTERM);

  /* Remove socket and PID. */
  unlink (pidpath);
  unlink (sockpath);

  /* Remove tmpdir. */
  snprintf (cmd, sizeof cmd, "rm -rf %s", tmpdir);
  if (system (cmd) != 0) perror ("rm");
}

static int
get_export (void *opaque, const char *name, const char *desc)
{
  char *copy;

  copy = strdup (name);
  if (copy == NULL || string_vector_append (&exports, copy) == -1) {
    perror ("malloc");
    exit (EXIT_FAILURE);
  }

  return 0;
}
