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
#include <errno.h>
#include <assert.h>

#include <libnbd.h>

/* This test checks the conversion from OCaml Unix.error to errno (in
 * the plugin) to NBD_E* (over the wire) and back to errno (in
 * libnbd).
 *
 * Reading at various sector offsets in the associated plugin
 * (test_ocaml_errorcodes_plugin.ml) produces predictable error codes.
 */
static struct { uint64_t offset; int expected_errno; } tests[] = {
  { 1*512, EPERM },
  { 2*512, EIO },
  { 3*512, ENOMEM },
  { 4*512, ESHUTDOWN },
  { 5*512, EINVAL },
  { 0 }
};

int
main (int argc, char *argv[])
{
  struct nbd_handle *nbd;
  char buf[512];
  size_t i;
  int actual_errno;

#ifdef __APPLE__
  printf ("%s: loading the OCaml plugin fails on macOS, skipping\n",
          argv[0]);
  exit (77);
#endif

  nbd = nbd_create ();
  if (nbd == NULL) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  if (nbd_connect_command (nbd,
                           (char *[]) {
                             "nbdkit", "-s", "--exit-with-parent",
                             "./test-ocaml-errorcodes-plugin.so",
                             NULL }) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  assert (nbd_pread (nbd, buf, 512, 0, 0) == 0);

  for (i = 0; tests[i].offset != 0; ++i) {
    assert (nbd_pread (nbd, buf, 512, tests[i].offset, 0) == -1);
    actual_errno = nbd_get_errno ();
    if (actual_errno != tests[i].expected_errno) {
      fprintf (stderr, "%s: FAIL: actual errno = %d expected errno = %d\n",
               argv[0], actual_errno, tests[i].expected_errno);
      exit (EXIT_FAILURE);
    }
  }

  nbd_close (nbd);
  exit (EXIT_SUCCESS);
}
