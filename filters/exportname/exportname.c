/* nbdkit
 * Copyright (C) 2020 Red Hat Inc.
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
#include <stdbool.h>
#include <string.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "open_memstream.h"
#include "utils.h"

static const char *default_export;
static enum {
  LIST_KEEP,
  LIST_ERROR,
  LIST_EMPTY,
  LIST_DEFAULT,
  LIST_EXPLICIT,
} list;
static bool strict;
static enum {
  DESC_KEEP,
  DESC_NONE,
  DESC_FIXED,
  DESC_SCRIPT,
} desc_mode;
static const char *desc;
struct nbdkit_exports *exports;

static void
exportname_load (void)
{
  exports = nbdkit_exports_new ();
  if (!exports) {
    nbdkit_error ("malloc: %m");
    exit (EXIT_FAILURE);
  }
}

static void
exportname_unload (void)
{
  nbdkit_exports_free (exports);
}

/* Called for each key=value passed on the command line. */
static int
exportname_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
                   const char *key, const char *value)
{
  int r;

  if (strcmp (key, "default-export") == 0 ||
      strcmp (key, "default_export") == 0) {
    default_export = value;
    return 0;
  }
  if (strcmp (key, "exportname-list") == 0 ||
      strcmp (key, "exportname_list") == 0) {
    if (strcmp (value, "keep") == 0)
      list = LIST_KEEP;
    else if (strcmp (value, "error") == 0)
      list = LIST_ERROR;
    else if (strcmp (value, "empty") == 0)
      list = LIST_EMPTY;
    else if (strcmp (value, "defaultonly") == 0 ||
             strcmp (value, "default-only") == 0)
      list = LIST_DEFAULT;
    else if (strcmp (value, "explicit") == 0)
      list = LIST_EXPLICIT;
    else {
      nbdkit_error ("unrecognized exportname-list mode: %s", value);
      return -1;
    }
    return 0;
  }
  if (strcmp (key, "exportname-strict") == 0 ||
      strcmp (key, "exportname_strict") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    strict = r;
    return 0;
  }
  if (strcmp (key, "exportname") == 0)
    return nbdkit_add_export (exports, value, NULL);
  if (strcmp (key, "exportdesc") == 0) {
    if (strcmp (value, "keep") == 0)
      desc_mode = DESC_KEEP;
    else if (strcmp (value, "none") == 0) {
      desc_mode = DESC_NONE;
      desc = NULL;
    }
    else if (strncmp (value, "fixed:", 6) == 0) {
      desc_mode = DESC_FIXED;
      desc = value + 6;
    }
    else if (strncmp (value, "script:", 7) == 0) {
      desc_mode = DESC_SCRIPT;
      desc = value + 7;
    }
    else {
      nbdkit_error ("unrecognized exportdesc mode: %s", value);
      return -1;
    }
    return 0;
  }
  return next (nxdata, key, value);
}

#define exportname_config_help \
  "default-export=<NAME>     Canonical name for the \"\" default export.\n" \
  "exportname-list=<MODE>    Which exports to advertise: keep (default), error,\n" \
  "                          empty, defaultonly, explicit.\n" \
  "exportname-strict=<BOOL>  Limit clients to explicit exports (default false).\n" \
  "exportname=<NAME>         Add an explicit export name, may be repeated.\n" \
  "exportdesc=<MODE>         Set descriptions according to mode: keep (default),\n" \
  "                          none, fixed:STRING, script:SCRIPT.\n" \

static const char *
get_desc (const char *name, const char *def)
{
  FILE *fp;
  CLEANUP_FREE char *cmd = NULL;
  size_t cmdlen = 0;
  char buf[4096]; /* Maximum NBD string; we truncate any longer response */
  size_t r;

  switch (desc_mode) {
  case DESC_KEEP:
    return def;
  case DESC_NONE:
  case DESC_FIXED:
    return desc;
  case DESC_SCRIPT:
    break;
  default:
    abort ();
  }

  /* Construct the command. */
  fp = open_memstream (&cmd, &cmdlen);
  if (fp == NULL) {
    nbdkit_debug ("open_memstream: %m");
    return NULL;
  }
  fprintf (fp, "export name; name=");
  shell_quote (name, fp);
  fprintf (fp, "\n%s\n", desc);
  if (close_memstream (fp) == -1) {
    nbdkit_debug ("memstream failed: %m");
    return NULL;
  }
  nbdkit_debug ("%s", cmd);
  fp = popen (cmd, "r");
  if (fp == NULL) {
    nbdkit_debug ("popen: %m");
    return NULL;
  }

  /* Now read the description */
  r = fread (buf, 1, sizeof buf, fp);
  if (r == 0 && ferror (fp)) {
    nbdkit_debug ("fread: %m");
    pclose (fp);
    return NULL;
  }
  pclose (fp);
  if (r && buf[r-1] == '\n')
    r--;
  return nbdkit_strndup_intern (buf, r);
}

static int
exportname_list_exports (nbdkit_next_list_exports *next,
                         nbdkit_backend *nxdata,
                         int readonly, int is_tls,
                         struct nbdkit_exports *exps)
{
  size_t i;
  struct nbdkit_exports *source;
  CLEANUP_EXPORTS_FREE struct nbdkit_exports *exps2 = NULL;

  switch (list) {
  case LIST_KEEP:
    source = exps2 = nbdkit_exports_new ();
    if (exps2 == NULL)
      return -1;
    if (next (nxdata, readonly, exps2) == -1)
      return -1;
    break;
  case LIST_ERROR:
    nbdkit_error ("export list restricted by policy");
    return -1;
  case LIST_EMPTY:
    return 0;
  case LIST_DEFAULT:
    return nbdkit_use_default_export (exps);
  case LIST_EXPLICIT:
    source = exports;
    break;
  default:
    abort ();
  }

  for (i = 0; i < nbdkit_exports_count (source); i++) {
    struct nbdkit_export e = nbdkit_get_export (source, i);

    if (nbdkit_add_export (exps, e.name,
                           get_desc (e.name, e.description)) == -1)
      return -1;
  }
  return 0;
}

static const char *
exportname_default_export (nbdkit_next_default_export *next,
                           nbdkit_backend *nxdata,
                           int readonly, int is_tls)
{
  size_t i;

  /* If we are strict, do not allow connection unless "" was advertised. */
  if (strict) {
    for (i = 0; i < nbdkit_exports_count (exports); i++) {
      if (nbdkit_get_export (exports, i).name[0] == '\0')
        return default_export ?: "";
    }
    return NULL;
  }

  if (default_export)
    return default_export;
  return next (nxdata, readonly);
}

struct handle {
  const char *name;
};

static void *
exportname_open (nbdkit_next_open *next, nbdkit_context *nxdata,
                 int readonly, const char *exportname, int is_tls)
{
  size_t i;
  struct handle *h;

  if (strict) {
    for (i = 0; i < nbdkit_exports_count (exports); i++) {
      if (strcmp (nbdkit_get_export (exports, i).name, exportname) == 0)
        break;
    }
    if (i == nbdkit_exports_count (exports)) {
      nbdkit_error ("export '%s not found", exportname);
      errno = ENOENT;
      return NULL;
    }
  }

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  h->name = nbdkit_strdup_intern (exportname);
  if (h->name == NULL) {
    free (h);
    return NULL;
  }
  if (next (nxdata, readonly, exportname) == -1) {
    free (h);
    return NULL;
  }

  return h;
}

static void
exportname_close (void *handle)
{
  free (handle);
}

static const char *
exportname_export_description (nbdkit_next *next,
                               void *handle)
{
  struct handle *h = handle;
  const char *def = NULL;

  if (desc_mode == DESC_KEEP)
    def = next->export_description (next);

  return get_desc (h->name, def);
}

static struct nbdkit_filter filter = {
  .name               = "exportname",
  .longname           = "nbdkit exportname filter",
  .load               = exportname_load,
  .unload             = exportname_unload,
  .config             = exportname_config,
  .config_help        = exportname_config_help,
  .list_exports       = exportname_list_exports,
  .default_export     = exportname_default_export,
  .open               = exportname_open,
  .close              = exportname_close,
  .export_description = exportname_export_description,
};

NBDKIT_REGISTER_FILTER(filter)
