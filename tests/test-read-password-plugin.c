#if 0
exec nbdkit cc "$0" "$@" EXTRA_CFLAGS="-I.. -I${SRCDIR:-.}/../include"
#endif
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

/* See test-read-password.sh and test-read-password-interactive.sh */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

static char *password;
static char *file;

static void
password_unload (void)
{
  free (password);
  free (file);
}

static int
password_config (const char *key, const char *value)
{
  if (strcmp (key, "password") == 0) {
    if (nbdkit_read_password (value, &password) == -1)
      return -1;
  }
  else if (strcmp (key, "file") == 0) {
    file = nbdkit_absolute_path (value);
    if (file == NULL)
      return -1;
  }
  else {
    nbdkit_error ("unknown parameter: %s", key);
    return -1;
  }
  return 0;
}

static int
password_config_complete (void)
{
  FILE *fp;

  if (!file || !password) {
    nbdkit_error ("file and password parameters are required");
    return -1;
  }
  fp = fopen (file, "w");
  if (!fp) {
    nbdkit_error ("%s: %m", file);
    return -1;
  }
  fprintf (fp, "%s", password);
  fclose (fp);
  return 0;
}

static int
password_get_ready (void)
{
  /* This plugin is for testing, so it never serves any data. */
  nbdkit_shutdown ();
  return 0;
}

static void *
password_open (int readonly)
{
  abort ();
}

static int64_t
password_get_size (void *handle)
{
  abort ();
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

static int
password_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
                uint32_t flags)
{
  abort ();
}

static struct nbdkit_plugin plugin = {
  .name              = "password",
  .version           = PACKAGE_VERSION,
  .unload            = password_unload,
  .config            = password_config,
  .config_complete   = password_config_complete,
  .get_ready         = password_get_ready,
  .open              = password_open,
  .get_size          = password_get_size,
  .pread             = password_pread,
};

NBDKIT_REGISTER_PLUGIN(plugin)
