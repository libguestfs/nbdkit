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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <nbdkit-plugin.h>

/* List of directories parsed from the command line. */
static char **dirs = NULL;
static size_t nr_dirs = 0;

/* genisoimage or mkisofs program, picked at compile time, but can be
 * overridden at run time.
 */
static const char *isoprog = ISOPROG;

/* Extra parameters for isoprog. */
static const char *params = NULL;

/* The temporary ISO. */
static int fd = -1;

/* Construct the temporary ISO. */
static void shell_quote (const char *str, FILE *fp);

static int
make_iso (void)
{
  const char *tmpdir;
  char *template;
  char *command = NULL;
  size_t command_len = 0;
  FILE *fp;
  size_t i;
  int r;

  /* Path for temporary file. */
  tmpdir = getenv ("TMPDIR");
  if (tmpdir == NULL)
    tmpdir = LARGE_TMPDIR;
  if (asprintf (&template, "%s/isoXXXXXX", tmpdir) == -1) {
    nbdkit_error ("asprintf: %m");
    return -1;
  }

  fd = mkstemp (template);
  if (fd == -1) {
    nbdkit_error ("mkstemp: %s: %m", template);
    free (template);
    return -1;
  }
  unlink (template);
  free (template);

  /* Construct the isoprog command. */
  fp = open_memstream (&command, &command_len);
  if (fp == NULL) {
    nbdkit_error ("open_memstream: %m");
    return -1;
  }

  fprintf (fp, "%s -quiet", isoprog);
  if (params)
    fprintf (fp, " %s", params);
  for (i = 0; i < nr_dirs; ++i) {
    fputc (' ', fp);
    shell_quote (dirs[i], fp);
  }
  /* Redirect output to the temporary file. */
  fprintf (fp, " >&%d", fd);

  if (fclose (fp) == EOF) {
    nbdkit_error ("memstream failed: %m");
    return -1;
  }

  /* Run the command. */
  nbdkit_debug ("%s", command);
  r = system (command);
  free (command);

  if (WIFEXITED (r) && WEXITSTATUS (r) != 0) {
    nbdkit_error ("external %s command failed with exit code %d",
                  isoprog, WEXITSTATUS (r));
    return -1;
  }
  else if (WIFSIGNALED (r)) {
    nbdkit_error ("external %s command was killed by signal %d",
                  isoprog, WTERMSIG (r));
    return -1;
  }
  else if (WIFSTOPPED (r)) {
    nbdkit_error ("external %s command was stopped by signal %d",
                  isoprog, WSTOPSIG (r));
    return -1;
  }

  return 0;
}

/* Print str to fp, shell quoting if necessary.  This comes from
 * libguestfs, but was written by me so I'm relicensing it to a BSD
 * license for nbdkit.
 */
static void
shell_quote (const char *str, FILE *fp)
{
  const char *safe_chars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_=,:/";
  size_t i, len;

  /* If the string consists only of safe characters, output it as-is. */
  len = strlen (str);
  if (len == strspn (str, safe_chars)) {
    fputs (str, fp);
    return;
  }

  /* Double-quote the string. */
  fputc ('"', fp);
  for (i = 0; i < len; ++i) {
    switch (str[i]) {
    case '$': case '`': case '\\': case '"':
      fputc ('\\', fp);
      /*FALLTHROUGH*/
    default:
      fputc (str[i], fp);
    }
  }
  fputc ('"', fp);
}

static void
iso_unload (void)
{
  size_t i;

  for (i = 0; i < nr_dirs; ++i)
    free (dirs[i]);
  free (dirs);

  if (fd >= 0)
    close (fd);
}

static int
iso_config (const char *key, const char *value)
{
  char **new_dirs;
  char *dir;

  if (strcmp (key, "dir") == 0) {
    dir = nbdkit_realpath (value);
    if (dir == NULL)
      return -1;

    new_dirs = realloc (dirs, sizeof (char *) * (nr_dirs + 1));
    if (new_dirs == NULL) {
      nbdkit_error ("realloc: %m");
      free (dir);
      return -1;
    }
    dirs = new_dirs;
    dirs[nr_dirs] = dir;
    nr_dirs++;
  }
  else if (strcmp (key, "params") == 0) {
    params = value;
  }
  else if (strcmp (key, "prog") == 0) {
    isoprog = value;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int
iso_config_complete (void)
{
  if (nr_dirs == 0) {
    nbdkit_error ("you must supply the dir=<DIRECTORY> parameter "
                  "after the plugin name on the command line");
    return -1;
  }

  if (make_iso () == -1)
    return -1;

  return 0;
}

#define iso_config_help \
  "dir=<DIRECTORY>     (required) The directory to serve.\n" \
  "params='<PARAMS>'              Extra parameters to pass.\n" \
  "prog=<ISOPROG>                 The program used to make ISOs." \

static void *
iso_open (int readonly)
{
  /* We don't need a per-connection handle, so this just acts as a
   * pointer to return.
   */
  static int h;

  return &h;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Get the file size. */
static int64_t
iso_get_size (void *handle)
{
  struct stat statbuf;

  if (fstat (fd, &statbuf) == -1) {
    nbdkit_error ("fstat: %m");
    return -1;
  }

  return statbuf.st_size;
}

/* Read data from the file. */
static int
iso_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
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
  .name              = "iso",
  .longname          = "nbdkit iso plugin",
  .version           = PACKAGE_VERSION,
  .unload            = iso_unload,
  .config            = iso_config,
  .config_complete   = iso_config_complete,
  .config_help       = iso_config_help,
  .magic_config_key  = "dir",
  .open              = iso_open,
  .get_size          = iso_get_size,
  .pread             = iso_pread,
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
