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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#undef NDEBUG /* Keep test strong even for nbdkit built without assertions */
#include <assert.h>

#include <sys/types.h>

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include "test.h"

#ifndef WIN32

/* 'test_start_nbdkit' below makes assumptions about the format of
 * these strings.
 */
#define TEST_NBDKIT_TEMPLATE "/tmp/nbdkitXXXXXX"
struct test_nbdkit {
  char tmpdir[          17     + 1]; /*          template,          NUL */
  char sockpath[        17 + 5 + 1]; /*          template, "/sock", NUL */
  char unixsockpath[5 + 17 + 5 + 1]; /* "unix:", template, "/sock", NUL */
  char pidpath[         17 + 4 + 1]; /*          template, "/pid",  NUL */
  pid_t pid;
  struct test_nbdkit *next;
};
const struct test_nbdkit template = {
  .tmpdir =               TEST_NBDKIT_TEMPLATE,
  .sockpath =             TEST_NBDKIT_TEMPLATE "/sock",
  .unixsockpath = "unix:" TEST_NBDKIT_TEMPLATE "/sock",
  .pidpath =              TEST_NBDKIT_TEMPLATE "/pid",
};

static struct test_nbdkit *head;

pid_t pid = 0;
const char *sock = NULL;
const char *server[2] = { NULL, NULL };

static void
cleanup (void)
{
  int status;
  struct test_nbdkit *next;
  const char *s;

  while (head) {
    if (head->pid > 0) {
      assert (!pid || pid == head->pid);
      pid = 0;

      /* This improves the stability when running the tests under
       * valgrind.  We have to wait a little for nbdkit close
       * callbacks to run.
       */
      s = getenv ("NBDKIT_VALGRIND");
      if (s && strcmp (s, "1") == 0)
        sleep (5);

      kill (head->pid, SIGTERM);

      /* Check the status of nbdkit is normal on exit. */
      if (waitpid (head->pid, &status, 0) == -1) {
        perror ("waitpid");
        _exit (EXIT_FAILURE);
      }
      if (WIFEXITED (status) && WEXITSTATUS (status) != 0) {
        _exit (WEXITSTATUS (status));
      }
      if (WIFSIGNALED (status)) {
        /* Note that nbdkit is supposed to catch the signal we send and
         * exit cleanly, so the following shouldn't happen.
         */
        fprintf (stderr, "nbdkit terminated by signal %d\n", WTERMSIG (status));
        _exit (EXIT_FAILURE);
      }
      if (WIFSTOPPED (status)) {
        fprintf (stderr, "nbdkit stopped by signal %d\n", WSTOPSIG (status));
        _exit (EXIT_FAILURE);
      }
    }

    unlink (head->pidpath);
    unlink (head->sockpath);
    rmdir (head->tmpdir);

    next = head->next;
    free (head);
    head = next;
  }
}

int
test_start_nbdkit (const char *arg, ...)
{
  size_t i, len;
  struct test_nbdkit *kit = malloc (sizeof *kit);
  bool exists;

  if (!kit) {
    perror ("malloc");
    return -1;
  }
  *kit = template;
  if (mkdtemp (kit->tmpdir) == NULL) {
    perror ("mkdtemp");
    free (kit);
    return -1;
  }
  len = strlen (kit->tmpdir);
  memcpy (kit->sockpath, kit->tmpdir, len);
  memcpy (kit->unixsockpath+5, kit->tmpdir, len);
  memcpy (kit->pidpath, kit->tmpdir, len);

  kit->pid = fork ();
  if (kit->pid == 0) {               /* Child (nbdkit). */
    const char *p;
#define MAX_ARGS 64
    const char *argv[MAX_ARGS+1];
    va_list args;

    argv[0] = "nbdkit";
    argv[1] = "-U";
    argv[2] = kit->sockpath;
    argv[3] = "-P";
    argv[4] = kit->pidpath;
    argv[5] = "-f";
    argv[6] = "-v";
    argv[7] = arg;
    i = 8;

    va_start (args, arg);
    while ((p = va_arg (args, const char *)) != NULL) {
      if (i >= MAX_ARGS)
        abort ();
      argv[i] = p;
      ++i;
    }
    va_end (args);
    argv[i] = NULL;

    execvp ("nbdkit", (char **) argv);
    perror ("exec: nbdkit");
    _exit (EXIT_FAILURE);
  }

  /* Ensure nbdkit is killed and temporary files are deleted when the
   * main program exits.
   */
  if (head)
    kit->next = head;
  else
    atexit (cleanup);
  head = kit;
  pid = kit->pid;
  sock = kit->sockpath;
  server[0] = kit->unixsockpath;

  /* Wait for the pidfile to turn up, which indicates that nbdkit has
   * started up successfully and is ready to serve requests.  However
   * if 'pid' exits in this time it indicates a failure to start up.
   * Also there is a timeout in case nbdkit hangs.
   */
  for (i = 0; i < NBDKIT_START_TIMEOUT; ++i) {
    if (waitpid (pid, NULL, WNOHANG) == pid)
      goto early_exit;

    if (kill (pid, 0) == -1) {
      if (errno == ESRCH) {
      early_exit:
        fprintf (stderr,
                 "%s FAILED: nbdkit exited before starting to serve files\n",
                 program_name);
        pid = 0;
        return -1;
      }
      perror ("kill");
    }

    exists = access (kit->pidpath, F_OK) == 0;
    if (exists)
      break;

    sleep (1);
  }

  if (!exists) {
    fprintf (stderr,
             "%s: nbdkit did not create pidfile %s within "
             "%d seconds, continuing anyway\n",
             program_name, kit->pidpath, NBDKIT_START_TIMEOUT);
  }

  return 0;
}

#else /* WIN32 */

/* All of the above code will require a lot of porting work for
 * Windows.  At the moment the test gets skipped.
 */
int
test_start_nbdkit (const char *arg, ...)
{
  fprintf (stderr, "%s: test skipped because not ported to Windows.\n",
           program_name);
  exit (77);
}

#endif
