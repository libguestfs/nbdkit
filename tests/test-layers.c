/* nbdkit
 * Copyright (C) 2018 Red Hat Inc.
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

/* This test constructs a plugin and 3 layers of filters:
 *
 *     NBD     ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌────────┐
 *  client ───▶│ filter3 │───▶│ filter2 │───▶│ filter1 │───▶│ plugin │
 * request     └─────────┘    └─────────┘    └─────────┘    └────────┘
 *
 * We then run every possible request and ensure that each method in
 * each filter and the plugin is called in the right order.  This
 * cannot be done with libguestfs or qemu-io, instead we must make NBD
 * client requests over a socket directly.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <pthread.h>

#include "byte-swapping.h"
#include "exit-with-parent.h"
#include "protocol.h"           /* From nbdkit core. */

/* Declare program_name. */
#if HAVE_DECL_PROGRAM_INVOCATION_SHORT_NAME == 1
#include <errno.h>
#define program_name program_invocation_short_name
#else
#define program_name "nbdkit"
#endif

static void *start_log_capture (void *);
static void log_verify_seen (const char *msg);
static void log_verify_seen_in_order (const char *msg, ...)
  __attribute__((sentinel));
static void log_free (void);

int
main (int argc, char *argv[])
{
  pid_t pid;
  int sfd[2];
  int pfd[2];
  int err;
  pthread_t thread;
  int sock;
  struct new_handshake handshake;
  uint32_t cflags;
  struct new_option option;
  struct new_handshake_finish handshake_finish;
  uint16_t eflags;
  struct request request;
  struct simple_reply reply;
  char data[512];

#ifndef HAVE_EXIT_WITH_PARENT
  printf ("%s: this test requires --exit-with-parent functionality\n",
          program_name);
  exit (77);
#endif

  /* Socket for communicating with nbdkit. */
  if (socketpair (AF_LOCAL, SOCK_STREAM, 0, sfd) == -1) {
    perror ("socketpair");
    exit (EXIT_FAILURE);
  }
  sock = sfd[0];

  /* Start nbdkit. */
  if (pipe (pfd) == -1) {       /* Pipe for log messages. */
    perror ("pipe");
    exit (EXIT_FAILURE);
  }
  pid = fork ();
  if (pid == 0) {               /* Child. */
    dup2 (sfd[1], 0);
    dup2 (sfd[1], 1);
    close (pfd[0]);
    dup2 (pfd[1], 2);
    close (pfd[1]);
    execlp ("nbdkit", "nbdkit",
            "--exit-with-parent",
            "-fvns",
            /* Because of asynchronous shutdown with threads, finalize
             * isn't reliably called unless we disable parallel.
             */
            "-t", "1",
            "--filter", ".libs/test-layers-filter3.so",
            "--filter", ".libs/test-layers-filter2.so",
            "--filter", ".libs/test-layers-filter1.so",
            ".libs/test-layers-plugin.so",
            "foo=bar",
            NULL);
    perror ("exec: nbdkit");
    _exit (EXIT_FAILURE);
  }

  /* Parent (test). */
  close (sfd[1]);
  close (pfd[1]);

  fprintf (stderr, "%s: nbdkit running\n", program_name);

  /* Start a thread which will just listen on the pipe and
   * place the log messages in a memory buffer.
   */
  err = pthread_create (&thread, NULL, start_log_capture, &pfd[0]);
  if (err) {
    errno = err;
    perror ("pthread_create");
    exit (EXIT_FAILURE);
  }
  err = pthread_detach (thread);
  if (err) {
    errno = err;
    perror ("pthread_detach");
    exit (EXIT_FAILURE);
  }

  /* Note for the purposes of this test we're not very careful about
   * checking for errors (except for the bare minimum) or handling the
   * full NBD protocol.  This is because we can be certain about
   * exactly which server we are connecting to and what it supports.
   * Don't use this as example code for connecting to NBD servers.
   *
   * Expect to receive newstyle handshake.
   */
  if (recv (sock, &handshake, sizeof handshake,
            MSG_WAITALL) != sizeof handshake) {
    perror ("recv: handshake");
    exit (EXIT_FAILURE);
  }
  if (memcmp (handshake.nbdmagic, "NBDMAGIC", 8) != 0 ||
      be64toh (handshake.version) != NEW_VERSION) {
    fprintf (stderr, "%s: unexpected NBDMAGIC or version\n",
             program_name);
    exit (EXIT_FAILURE);
  }

  /* Send client flags. */
  cflags = htobe32 (be16toh (handshake.gflags));
  if (send (sock, &cflags, sizeof cflags, 0) != sizeof cflags) {
    perror ("send: flags");
    exit (EXIT_FAILURE);
  }

  /* Send NBD_OPT_EXPORT_NAME with no export name. */
  option.version = htobe64 (NEW_VERSION);
  option.option = htobe32 (NBD_OPT_EXPORT_NAME);
  option.optlen = htobe32 (0);
  if (send (sock, &option, sizeof option, 0) != sizeof option) {
    perror ("send: option");
    exit (EXIT_FAILURE);
  }

  /* Receive handshake finish. */
  if (recv (sock, &handshake_finish, sizeof handshake_finish - 124,
            MSG_WAITALL) != sizeof handshake_finish - 124) {
    perror ("recv: handshake finish");
    exit (EXIT_FAILURE);
  }

  /* Verify export size (see tests/test-layers-plugin.c). */
  if (be64toh (handshake_finish.exportsize) != 1024) {
    fprintf (stderr, "%s: unexpected export size %" PRIu64 " != 1024\n",
             program_name, be64toh (handshake_finish.exportsize));
    exit (EXIT_FAILURE);
  }

  /* Verify export flags. */
  eflags = be16toh (handshake_finish.eflags);
  if ((eflags & NBD_FLAG_READ_ONLY) != 0) {
    fprintf (stderr, "%s: unexpected eflags: NBD_FLAG_READ_ONLY not clear\n",
             program_name);
    exit (EXIT_FAILURE);
  }
  if ((eflags & NBD_FLAG_SEND_FLUSH) == 0) {
    fprintf (stderr, "%s: unexpected eflags: NBD_FLAG_SEND_FLUSH not set\n",
             program_name);
    exit (EXIT_FAILURE);
  }
  if ((eflags & NBD_FLAG_SEND_FUA) == 0) {
    fprintf (stderr, "%s: unexpected eflags: NBD_FLAG_SEND_FUA not set\n",
             program_name);
    exit (EXIT_FAILURE);
  }
  if ((eflags & NBD_FLAG_ROTATIONAL) == 0) {
    fprintf (stderr, "%s: unexpected eflags: NBD_FLAG_ROTATIONAL not set\n",
             program_name);
    exit (EXIT_FAILURE);
  }
  if ((eflags & NBD_FLAG_SEND_TRIM) == 0) {
    fprintf (stderr, "%s: unexpected eflags: NBD_FLAG_SEND_TRIM not set\n",
             program_name);
    exit (EXIT_FAILURE);
  }
  if ((eflags & NBD_FLAG_SEND_WRITE_ZEROES) == 0) {
    fprintf (stderr,
             "%s: unexpected eflags: NBD_FLAG_SEND_WRITE_ZEROES not set\n",
             program_name);
    exit (EXIT_FAILURE);
  }

  /* Sleep briefly to allow the log to catch up. */
  sleep (1);

  /* Verify expected log messages were seen during the handshake and
   * option negotiation phases.
   */

  /* Plugin and 3 filters should run the load method in any order. */
  log_verify_seen ("test_layers_plugin_load");
  log_verify_seen ("filter1: test_layers_filter_load");
  log_verify_seen ("filter2: test_layers_filter_load");
  log_verify_seen ("filter3: test_layers_filter_load");

  /* config methods called in order. */
  log_verify_seen_in_order
    ("testlayersfilter3: config key=foo, value=bar",
     "filter3: test_layers_filter_config",
     "testlayersfilter2: config key=foo, value=bar",
     "filter2: test_layers_filter_config",
     "testlayersfilter1: config key=foo, value=bar",
     "filter1: test_layers_filter_config",
     "testlayersplugin: config key=foo, value=bar",
     "test_layers_plugin_config",
     NULL);

  /* config_complete methods called in order. */
  log_verify_seen_in_order
    ("testlayersfilter3: config_complete",
     "filter3: test_layers_filter_config_complete",
     "testlayersfilter2: config_complete",
     "filter2: test_layers_filter_config_complete",
     "testlayersfilter1: config_complete",
     "filter1: test_layers_filter_config_complete",
     "testlayersplugin: config_complete",
     "test_layers_plugin_config_complete",
     NULL);

  /* open methods called in order. */
  log_verify_seen_in_order
    ("testlayersfilter3: open readonly=0",
     "filter3: test_layers_filter_open",
     "testlayersfilter2: open readonly=0",
     "filter2: test_layers_filter_open",
     "testlayersfilter1: open readonly=0",
     "filter1: test_layers_filter_open",
     "testlayersplugin: open readonly=0",
     "test_layers_plugin_open",
     NULL);

  /* prepare methods called in order.
   *
   * Note that prepare methods only exist for filters, and they must
   * be called from inner to outer (but finalize methods below are
   * called the other way around).
   */
  log_verify_seen_in_order
    ("filter1: test_layers_filter_prepare",
     "filter2: test_layers_filter_prepare",
     "filter3: test_layers_filter_prepare",
     NULL);

  /* get_size methods called in order. */
  log_verify_seen_in_order
    ("filter3: test_layers_filter_get_size",
     "filter2: test_layers_filter_get_size",
     "filter1: test_layers_filter_get_size",
     "test_layers_plugin_get_size",
     NULL);

  /* can_* / is_* methods called in order. */
  log_verify_seen_in_order
    ("filter3: test_layers_filter_can_write",
     "filter2: test_layers_filter_can_write",
     "filter1: test_layers_filter_can_write",
     "test_layers_plugin_can_write",
     NULL);
  log_verify_seen_in_order
    ("filter3: test_layers_filter_can_zero",
     "filter2: test_layers_filter_can_zero",
     "filter1: test_layers_filter_can_zero",
     "test_layers_plugin_can_zero",
     NULL);
  log_verify_seen_in_order
    ("filter3: test_layers_filter_can_trim",
     "filter2: test_layers_filter_can_trim",
     "filter1: test_layers_filter_can_trim",
     "test_layers_plugin_can_trim",
     NULL);
  log_verify_seen_in_order
    ("filter3: test_layers_filter_can_fua",
     "filter2: test_layers_filter_can_fua",
     "filter1: test_layers_filter_can_fua",
     "test_layers_plugin_can_fua",
     NULL);
  log_verify_seen_in_order
    ("filter3: test_layers_filter_can_flush",
     "filter2: test_layers_filter_can_flush",
     "filter1: test_layers_filter_can_flush",
     "test_layers_plugin_can_flush",
     NULL);
  log_verify_seen_in_order
    ("filter3: test_layers_filter_is_rotational",
     "filter2: test_layers_filter_is_rotational",
     "filter1: test_layers_filter_is_rotational",
     "test_layers_plugin_is_rotational",
     NULL);
  log_verify_seen_in_order
    ("filter3: test_layers_filter_can_multi_conn",
     "filter2: test_layers_filter_can_multi_conn",
     "filter1: test_layers_filter_can_multi_conn",
     "test_layers_plugin_can_multi_conn",
     NULL);

  fprintf (stderr, "%s: protocol connected\n", program_name);

  /* Send one command of each type. */
  request.magic = htobe32 (NBD_REQUEST_MAGIC);
  request.handle = htobe64 (0);

  request.type = htobe16 (NBD_CMD_READ);
  request.offset = htobe64 (0);
  request.count = htobe32 (512);
  request.flags = htobe16 (0);
  if (send (sock, &request, sizeof request, 0) != sizeof request) {
    perror ("send: NBD_CMD_READ");
    exit (EXIT_FAILURE);
  }
  if (recv (sock, &reply, sizeof reply, MSG_WAITALL) != sizeof reply) {
    perror ("recv: NBD_CMD_READ reply");
    exit (EXIT_FAILURE);
  }
  if (reply.error != NBD_SUCCESS) {
    fprintf (stderr, "%s: NBD_CMD_READ failed with %d\n",
             program_name, reply.error);
    exit (EXIT_FAILURE);
  }
  if (recv (sock, data, sizeof data, MSG_WAITALL) != sizeof data) {
    perror ("recv: NBD_CMD_READ data");
    exit (EXIT_FAILURE);
  }

  sleep (1);
  log_verify_seen_in_order
    ("testlayersfilter3: pread count=512 offset=0 flags=0x0",
     "filter3: test_layers_filter_pread",
     "testlayersfilter2: pread count=512 offset=0 flags=0x0",
     "filter2: test_layers_filter_pread",
     "testlayersfilter1: pread count=512 offset=0 flags=0x0",
     "filter1: test_layers_filter_pread",
     "testlayersplugin: debug: pread count=512 offset=0",
     "test_layers_plugin_pread",
     NULL);

  request.type = htobe16 (NBD_CMD_WRITE);
  request.offset = htobe64 (0);
  request.count = htobe32 (512);
  request.flags = htobe16 (0);
  if (send (sock, &request, sizeof request, 0) != sizeof request) {
    perror ("send: NBD_CMD_WRITE");
    exit (EXIT_FAILURE);
  }
  if (send (sock, data, sizeof data, 0) != sizeof data) {
    perror ("send: NBD_CMD_WRITE data");
    exit (EXIT_FAILURE);
  }
  if (recv (sock, &reply, sizeof reply, MSG_WAITALL) != sizeof reply) {
    perror ("recv: NBD_CMD_WRITE");
    exit (EXIT_FAILURE);
  }
  if (reply.error != NBD_SUCCESS) {
    fprintf (stderr, "%s: NBD_CMD_WRITE failed with %d\n",
             program_name, reply.error);
    exit (EXIT_FAILURE);
  }

  sleep (1);
  log_verify_seen_in_order
    ("testlayersfilter3: pwrite count=512 offset=0 flags=0x0",
     "filter3: test_layers_filter_pwrite",
     "testlayersfilter2: pwrite count=512 offset=0 flags=0x0",
     "filter2: test_layers_filter_pwrite",
     "testlayersfilter1: pwrite count=512 offset=0 flags=0x0",
     "filter1: test_layers_filter_pwrite",
     "testlayersplugin: debug: pwrite count=512 offset=0",
     "test_layers_plugin_pwrite",
     NULL);

  request.type = htobe16 (NBD_CMD_FLUSH);
  request.offset = htobe64 (0);
  request.count = htobe32 (0);
  request.flags = htobe16 (0);
  if (send (sock, &request, sizeof request, 0) != sizeof request) {
    perror ("send: NBD_CMD_FLUSH");
    exit (EXIT_FAILURE);
  }
  if (recv (sock, &reply, sizeof reply, MSG_WAITALL) != sizeof reply) {
    perror ("recv: NBD_CMD_FLUSH");
    exit (EXIT_FAILURE);
  }
  if (reply.error != NBD_SUCCESS) {
    fprintf (stderr, "%s: NBD_CMD_FLUSH failed with %d\n",
             program_name, reply.error);
    exit (EXIT_FAILURE);
  }

  sleep (1);
  log_verify_seen_in_order
    ("testlayersfilter3: flush flags=0x0",
     "filter3: test_layers_filter_flush",
     "testlayersfilter2: flush flags=0x0",
     "filter2: test_layers_filter_flush",
     "testlayersfilter1: flush flags=0x0",
     "filter1: test_layers_filter_flush",
     "testlayersplugin: debug: flush",
     "test_layers_plugin_flush",
     NULL);

  request.type = htobe16 (NBD_CMD_TRIM);
  request.offset = htobe64 (0);
  request.count = htobe32 (512);
  request.flags = htobe16 (0);
  if (send (sock, &request, sizeof request, 0) != sizeof request) {
    perror ("send: NBD_CMD_TRIM");
    exit (EXIT_FAILURE);
  }
  if (recv (sock, &reply, sizeof reply, MSG_WAITALL) != sizeof reply) {
    perror ("recv: NBD_CMD_TRIM");
    exit (EXIT_FAILURE);
  }
  if (reply.error != NBD_SUCCESS) {
    fprintf (stderr, "%s: NBD_CMD_TRIM failed with %d\n",
             program_name, reply.error);
    exit (EXIT_FAILURE);
  }

  sleep (1);
  log_verify_seen_in_order
    ("testlayersfilter3: trim count=512 offset=0 flags=0x0",
     "filter3: test_layers_filter_trim",
     "testlayersfilter2: trim count=512 offset=0 flags=0x0",
     "filter2: test_layers_filter_trim",
     "testlayersfilter1: trim count=512 offset=0 flags=0x0",
     "filter1: test_layers_filter_trim",
     "testlayersplugin: debug: trim count=512 offset=0",
     "test_layers_plugin_trim",
     NULL);

  request.type = htobe16 (NBD_CMD_WRITE_ZEROES);
  request.offset = htobe64 (0);
  request.count = htobe32 (512);
  request.flags = htobe16 (0);
  if (send (sock, &request, sizeof request, 0) != sizeof request) {
    perror ("send: NBD_CMD_WRITE_ZEROES");
    exit (EXIT_FAILURE);
  }
  if (recv (sock, &reply, sizeof reply, MSG_WAITALL) != sizeof reply) {
    perror ("recv: NBD_CMD_WRITE_ZEROES");
    exit (EXIT_FAILURE);
  }
  if (reply.error != NBD_SUCCESS) {
    fprintf (stderr, "%s: NBD_CMD_WRITE_ZEROES failed with %d\n",
             program_name, reply.error);
    exit (EXIT_FAILURE);
  }

  sleep (1);
  log_verify_seen_in_order
    ("testlayersfilter3: zero count=512 offset=0 flags=0x1",
     "filter3: test_layers_filter_zero",
     "testlayersfilter2: zero count=512 offset=0 flags=0x1",
     "filter2: test_layers_filter_zero",
     "testlayersfilter1: zero count=512 offset=0 flags=0x1",
     "filter1: test_layers_filter_zero",
     "testlayersplugin: debug: zero count=512 offset=0 may_trim=1 fua=0",
     "test_layers_plugin_zero",
     NULL);

  /* Close the connection. */
  fprintf (stderr, "%s: closing the connection\n", program_name);
  request.type = htobe16 (NBD_CMD_DISC);
  request.offset = htobe64 (0);
  request.count = htobe32 (0);
  request.flags = htobe16 (0);
  if (send (sock, &request, sizeof request, 0) != sizeof request) {
    perror ("send: NBD_CMD_DISC");
    exit (EXIT_FAILURE);
  }
  /* (no reply from NBD_CMD_DISC) */
  close (sock);

  /* Clean up the child process. */
  if (waitpid (pid, NULL, 0) == -1)
    perror ("waitpid");

  /* finalize methods called in reverse order of prepare */
  sleep (1);
  log_verify_seen_in_order
    ("filter3: test_layers_filter_finalize",
     "filter2: test_layers_filter_finalize",
     "filter1: test_layers_filter_finalize",
     NULL);

  /* close methods called in order */
  log_verify_seen_in_order
    ("filter3: test_layers_filter_close",
     "filter2: test_layers_filter_close",
     "filter1: test_layers_filter_close",
     "test_layers_plugin_close",
     NULL);

  /* unload methods should be run in any order. */
  log_verify_seen ("test_layers_plugin_unload");
  log_verify_seen ("filter1: test_layers_filter_unload");
  log_verify_seen ("filter2: test_layers_filter_unload");
  log_verify_seen ("filter3: test_layers_filter_unload");

  log_free ();

  exit (EXIT_SUCCESS);
}

/* The log from nbdkit is captured in a separate thread. */
static char *log = NULL;
static size_t log_len = 0;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

static void *
start_log_capture (void *arg)
{
  int fd = *(int *)arg;
  size_t allocated = 0;
  ssize_t r;

  for (;;) {
    pthread_mutex_lock (&log_lock);
    if (allocated <= log_len) {
      allocated += 4096;
      log = realloc (log, allocated);
      if (log == NULL) {
        perror ("log: realloc");
        exit (EXIT_FAILURE);
      }
    }
    pthread_mutex_unlock (&log_lock);

    r = read (fd, &log[log_len], allocated-log_len);
    if (r == -1) {
      perror ("log: read");
      exit (EXIT_FAILURE);
    }
    if (r == 0)
      break;

    /* Dump the log as we receive it to stderr, for debugging. */
    if (write (2, &log[log_len], r) == -1)
      perror ("log: write");

    pthread_mutex_lock (&log_lock);
    log_len += r;
    pthread_mutex_unlock (&log_lock);
  }

  /* nbdkit closed the connection. */
  pthread_exit (NULL);
}

/* These functions are called from the main thread to verify messages
 * appeared as expected in the log.
 *
 * NB: The log buffer is NOT \0-terminated.
 */

static void no_message_error (const char *msg) __attribute__((noreturn));

static void
no_message_error (const char *msg)
{
  fprintf (stderr, "%s: did not find expected message \"%s\"\n",
           program_name, msg);
  exit (EXIT_FAILURE);
}

static void
log_verify_seen (const char *msg)
{
  pthread_mutex_lock (&log_lock);
  if (memmem (log, log_len, msg, strlen (msg)) == NULL)
    no_message_error (msg);
  pthread_mutex_unlock (&log_lock);
}

static void messages_out_of_order (const char *msg1, const char *msg2)
  __attribute__((noreturn));

static void
messages_out_of_order (const char *msg1, const char *msg2)
{
  fprintf (stderr, "%s: message \"%s\" expected before message \"%s\"\n",
           program_name, msg1, msg2);
  exit (EXIT_FAILURE);
}

static void
log_verify_seen_in_order (const char *msg, ...)
{
  va_list args;
  void *prev, *curr;
  const char *prev_msg, *curr_msg;

  pthread_mutex_lock (&log_lock);

  prev = memmem (log, log_len, msg, strlen (msg));
  if (prev == NULL) no_message_error (msg);
  prev_msg = msg;

  va_start (args, msg);
  while ((curr_msg = va_arg (args, char *)) != NULL) {
    curr = memmem (log, log_len, curr_msg, strlen (curr_msg));
    if (curr == NULL) no_message_error (curr_msg);
    if (prev > curr) messages_out_of_order (prev_msg, curr_msg);
    prev_msg = curr_msg;
    prev = curr;
  }
  va_end (args);

  pthread_mutex_unlock (&log_lock);
}

static void
log_free (void)
{
  pthread_mutex_lock (&log_lock);
  free (log);
  log = NULL;
  log_len = 0;
  pthread_mutex_unlock (&log_lock);
}
