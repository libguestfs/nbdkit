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
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#undef NDEBUG /* Keep test strong even for nbdkit built without assertions */
#include <assert.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "getline.h"

static const char *msg = "input";

/* Check whether stdin/out match /dev/null */
static bool
stdio_check (void)
{
  static int dn = -1;
  struct stat st1, st2;

  if (dn == -1) {
    dn = open ("/dev/null", O_RDONLY);
    assert (dn > STDERR_FILENO);
  }
  if (fstat (dn, &st1) == -1)
    assert (false);

  if (fstat (STDIN_FILENO, &st2) == -1)
    assert (false);
  if (st1.st_dev != st2.st_dev || st1.st_ino != st2.st_ino)
    return false;

  if (fstat (STDOUT_FILENO, &st2) == -1)
    assert (false);
  if (st1.st_dev != st2.st_dev || st1.st_ino != st2.st_ino)
    return false;

  return true;
}

static void
stdio_dump_plugin (void)
{
  char *buf = NULL;
  size_t len = 0;
  bool check = stdio_check ();

  assert (check == false);

  /* Reading from stdin during .dump_plugin is unusual, but not forbidden */
  if (getline (&buf, &len, stdin) == -1)
    assert (false);
  /* The point of .dump_plugin is to extend details sent to stdout */
  printf ("%s=%s\n", msg, buf);
  free (buf);
}

static int
stdio_config (const char *key, const char *value)
{
  bool check = stdio_check ();
  assert (check == false);
  msg = key;
  return 0;
}

static int
stdio_config_complete (void)
{
  bool check = stdio_check ();
  assert (check == false);
  if (nbdkit_stdio_safe ()) {
    char *buf = NULL;
    size_t len = 0;

    /* Reading from stdin during .config_complete is safe except under -s */
    if (getline (&buf, &len, stdin) == -1)
      assert (false);
    /* Output during .config_complete is unusual, but not forbidden */
    printf ("%s=%s\n", msg, buf);
    free (buf);
  }
  return 0;
}

static int
stdio_get_ready (void)
{
  bool check = stdio_check ();
  assert (check == false);
  return 0;
}

static int
stdio_after_fork (void)
{
  bool check = stdio_check ();
  assert (check == true);
  return 0;
}

static void *
stdio_open (int readonly)
{
  bool check = stdio_check ();
  assert (check == true);
  return NBDKIT_HANDLE_NOT_NEEDED;
}

static int64_t
stdio_get_size (void *handle)
{
  bool check = stdio_check ();
  assert (check == true);
  return 1024*1024;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

static int
stdio_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
             uint32_t flags)
{
  bool check = stdio_check ();
  assert (check == true);
  memset (buf, 0, count);
  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "stdio",
  .version           = PACKAGE_VERSION,
  .dump_plugin       = stdio_dump_plugin,
  .config            = stdio_config,
  .config_complete   = stdio_config_complete,
  .get_ready         = stdio_get_ready,
  .after_fork        = stdio_after_fork,
  .open              = stdio_open,
  .get_size          = stdio_get_size,
  .pread             = stdio_pread,
};

NBDKIT_REGISTER_PLUGIN (plugin)
