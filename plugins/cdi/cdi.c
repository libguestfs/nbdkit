/* nbdkit
 * Copyright (C) 2018-2020 Red Hat Inc.
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
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "utils.h"

/* Parameters. */
static const char *name;    /* Name or URI of container image. */
static int layer = 0;       /* Layer (may be negative to count from end). */

/* The script that we run to pull and unpack the image. */
static const char script[] =
  /* Exit on errors. */
  "set -e\n"
  /* Avoid stdin/stdout leaking (because of nbdkit -s).
   * XXX Capture errors to a temporary file.
   */
  "exec </dev/null >/dev/null\n"
  /* Create a temporary directory to extract the image to. */
  "d=\"$tmpfile.d\"\n"
  "podman pull \"$name\"\n"
  "podman save --format oci-dir -o \"$d\" \"$name\"\n"
  "f=\"$d/$( jq -r \".layers[$layer].digest\" < \"$d/manifest.json\" |\n"
  "          cut -d: -f2 )\"\n"
  "if ! test -f \"$f\"; then\n"
  "    echo \"cdi: could not extract layer\"\n"
  "    rm -rf \"$d\"\n"
  "    exit 1\n"
  "fi\n"
  "mv \"$f\" \"$tmpfile\"\n"
  "rm -rf \"$d\"\n";

/* The temporary file. */
static int fd = -1;

/* Construct the temporary file. */
static int
make_layer (void)
{
  const char *tmpdir;
  CLEANUP_FREE char *template = NULL;
  CLEANUP_FREE char *command = NULL;
  size_t command_len = 0;
  FILE *fp;
  int r;

  /* Path for temporary file. */
  tmpdir = getenv ("TMPDIR");
  if (tmpdir == NULL)
    tmpdir = LARGE_TMPDIR;
  if (asprintf (&template, "%s/imageXXXXXX", tmpdir) == -1) {
    nbdkit_error ("asprintf: %m");
    return -1;
  }

  fd = mkstemp (template);
  if (fd == -1) {
    nbdkit_error ("mkstemp: %s: %m", template);
    return -1;
  }

  /* Construct the podman script. */
  fp = open_memstream (&command, &command_len);
  if (fp == NULL) {
    nbdkit_error ("open_memstream: %m");
    return -1;
  }
  fprintf (fp, "name="); shell_quote (name, fp); putc ('\n', fp);
  fprintf (fp, "layer=%d\n", layer);
  fprintf (fp, "tmpfile="); shell_quote (template, fp); putc ('\n', fp);
  fprintf (fp, "\n");
  fprintf (fp, "%s", script);
  if (fclose (fp) == EOF) {
    nbdkit_error ("memstream failed: %m");
    return -1;
  }

  /* Run the command. */
  nbdkit_debug ("%s", command);
  r = system (command);
  if (exit_status_to_nbd_error (r, "podman") == -1)
    return -1;

  /* Expect that the script creates 'template'. */
  if (access (template, F_OK) != 0) {
    nbdkit_error ("internal error: expected %s to be created", template);
    return -1;
  }

  /* Since the script likely overwrites the file, we need to reopen it. */
  close (fd);
  fd = open (template, O_RDONLY|O_CLOEXEC);
  if (fd == -1) {
    nbdkit_error ("open: %s: %m", template);
    unlink (template);
    return -1;
  }

  /* Since we've opened the file, we can unlink it. */
  unlink (template);

  return 0;
}

static void
cdi_unload (void)
{
  if (fd >= 0)
    close (fd);
}

static int
cdi_config (const char *key, const char *value)
{
  if (strcmp (key, "name") == 0)
    name = value;
  else if (strcmp (key, "layer") == 0) {
    if (nbdkit_parse_int ("layer", value, &layer) == -1)
      return -1;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int
cdi_config_complete (void)
{
  if (name == NULL) {
    nbdkit_error ("you must supply the 'name' parameter "
                  "after the plugin name on the command line");
    return -1;
  }

  return 0;
}

#define cdi_config_help \
  "name=NAME[:TAG|@DIGEST] (required) Name or URI of container image.\n" \
  "layer=<N>                          Layer of image to export."

static int
cdi_get_ready (void)
{
  return make_layer ();
}

static void *
cdi_open (int readonly)
{
  return NBDKIT_HANDLE_NOT_NEEDED;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Get the file size. */
static int64_t
cdi_get_size (void *handle)
{
  struct stat statbuf;

  if (fstat (fd, &statbuf) == -1) {
    nbdkit_error ("fstat: %m");
    return -1;
  }

  return statbuf.st_size;
}

/* Serves the same data over multiple connections. */
static int
cdi_can_multi_conn (void *handle)
{
  return 1;
}

static int
cdi_can_cache (void *handle)
{
  /* Let nbdkit call pread to populate the file system cache. */
  return NBDKIT_CACHE_EMULATE;
}

/* Read data from the file. */
static int
cdi_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  while (count > 0) {
    ssize_t r = pread (fd, buf, count, offset);
    if (r == -1) {
      nbdkit_error ("pread: %m");
      return -1;
    }
    if (r == 0) {
      nbdkit_error ("pread: unexpected end of file");
      return -1;
    }
    buf += r;
    count -= r;
    offset += r;
  }

  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "cdi",
  .longname          = "nbdkit containerized data importer plugin",
  .version           = PACKAGE_VERSION,
  .unload            = cdi_unload,
  .config            = cdi_config,
  .config_complete   = cdi_config_complete,
  .config_help       = cdi_config_help,
  .magic_config_key  = "name",
  .get_ready         = cdi_get_ready,
  .open              = cdi_open,
  .get_size          = cdi_get_size,
  .can_multi_conn    = cdi_can_multi_conn,
  .can_cache         = cdi_can_cache,
  .pread             = cdi_pread,
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
