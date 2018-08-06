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
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#include <nbdkit-filter.h>

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

struct error_settings {
  int error;                   /* errno, eg. EIO */
  double rate;                 /* rate, 0.0 = never, 1.0 = always */
  char *file;                  /* error file, NULL = no file */
};

#define ERROR_DEFAULT { .error = EIO, .rate = 0, .file = NULL }

/* Settings for each type of request, read from the command line. */
static struct error_settings pread_settings = ERROR_DEFAULT;
static struct error_settings pwrite_settings = ERROR_DEFAULT;
static struct error_settings trim_settings = ERROR_DEFAULT;
static struct error_settings zero_settings = ERROR_DEFAULT;

static void
error_unload (void)
{
  free (pread_settings.file);
  free (pwrite_settings.file);
  free (trim_settings.file);
  free (zero_settings.file);
}

static const struct { const char *name; int error; } errors[] = {
  { "EPERM", EPERM },
  { "EIO", EIO },
  { "ENOMEM", ENOMEM },
  { "EINVAL", EINVAL },
  { "ENOSPC", ENOSPC },
  { "ESHUTDOWN", ESHUTDOWN },
  { NULL }
};

static const char *
error_as_string (int error)
{
  size_t i;

  for (i = 0; errors[i].name != NULL; ++i) {
    if (errors[i].error == error)
      return errors[i].name;
  }
  abort ();
}

static int
parse_error (const char *key, const char *value, int *retp)
{
  size_t i;

  for (i = 0; errors[i].name != NULL; ++i) {
    if (strcmp (value, errors[i].name) == 0) {
      *retp = errors[i].error;
      return 0;
    }
  }

  nbdkit_error ("%s: unknown error name '%s'", key, value);
  return -1;
}

static int
parse_error_rate (const char *key, const char *value, double *retp)
{
  double d;

  if (sscanf (value, "%lg%%", &d) == 1) /* percentage */
    d /= 100.0;
  else if (sscanf (value, "%lg", &d) == 1) /* probability */
    ;
  else {
    nbdkit_error ("%s: could not parse rate '%s'", key, value);
    return -1;
  }
  if (d < 0 || d > 1) {
    nbdkit_error ("%s: rate out of range: '%s' parsed as %g", key, value, d);
    return -1;
  }
  *retp = d;
  return 0;
}

/* Called for each key=value passed on the command line. */
static int
error_config (nbdkit_next_config *next, void *nxdata,
              const char *key, const char *value)
{
  int i;
  double d;

  if (strcmp (key, "error") == 0) {
    if (parse_error (key, value, &i) == -1)
      return -1;
    pread_settings.error = pwrite_settings.error =
      trim_settings.error = zero_settings.error = i;
    return 0;
  }
  else if (strcmp (key, "error-pread") == 0)
    return parse_error (key, value, &pread_settings.error);
  else if (strcmp (key, "error-pwrite") == 0)
    return parse_error (key, value, &pwrite_settings.error);
  else if (strcmp (key, "error-trim") == 0)
    return parse_error (key, value, &trim_settings.error);
  else if (strcmp (key, "error-zero") == 0)
    return parse_error (key, value, &zero_settings.error);

  else if (strcmp (key, "error-rate") == 0) {
    if (parse_error_rate (key, value, &d) == -1)
      return -1;
    pread_settings.rate = pwrite_settings.rate =
      trim_settings.rate = zero_settings.rate = d;
    return 0;
  }
  else if (strcmp (key, "error-pread-rate") == 0)
    return parse_error_rate (key, value, &pread_settings.rate);
  else if (strcmp (key, "error-pwrite-rate") == 0)
    return parse_error_rate (key, value, &pwrite_settings.rate);
  else if (strcmp (key, "error-trim-rate") == 0)
    return parse_error_rate (key, value, &trim_settings.rate);
  else if (strcmp (key, "error-zero-rate") == 0)
    return parse_error_rate (key, value, &zero_settings.rate);

  /* NB: We are using nbdkit_absolute_path here because the trigger
   * file probably doesn't exist yet.
   */
  else if (strcmp (key, "error-file") == 0) {
    free (pread_settings.file);
    pread_settings.file = nbdkit_absolute_path (value);
    free (pwrite_settings.file);
    pwrite_settings.file = nbdkit_absolute_path (value);
    free (trim_settings.file);
    trim_settings.file = nbdkit_absolute_path (value);
    free (zero_settings.file);
    zero_settings.file = nbdkit_absolute_path (value);
    return 0;
  }
  else if (strcmp (key, "error-pread-rate") == 0) {
    free (pread_settings.file);
    pread_settings.file = nbdkit_absolute_path (value);
    return 0;
  }
  else if (strcmp (key, "error-pwrite-rate") == 0) {
    free (pwrite_settings.file);
    pwrite_settings.file = nbdkit_absolute_path (value);
    return 0;
  }
  else if (strcmp (key, "error-trim-rate") == 0) {
    free (trim_settings.file);
    trim_settings.file = nbdkit_absolute_path (value);
    return 0;
  }
  else if (strcmp (key, "error-zero-rate") == 0) {
    free (zero_settings.file);
    zero_settings.file = nbdkit_absolute_path (value);
    return 0;
  }

  else
    return next (nxdata, key, value);
}

#define error_config_help \
  "error=EPERM|EIO|ENOMEM|EINVAL|ENOSPC|ESHUTDOWN\n" \
  "                               The error indication to return.\n" \
  "error-rate=0%..100%|0..1       Rate of errors to generate.\n" \
  "error-file=TRIGGER             Set trigger filename.\n" \
  "error-pread*, error-pwrite*, error-trim*, error-zero*\n" \
  "                               Apply settings only to read/write/trim/zero"

struct handle {
  struct random_data rd;
  char rd_state[32];
};

static void *
error_open (nbdkit_next_open *next, void *nxdata, int readonly)
{
  struct handle *h;
  time_t t;

  if (next (nxdata, readonly) == -1)
    return NULL;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }
  memset (&h->rd, 0, sizeof h->rd);
  time (&t);
  initstate_r (t, (char *) &h->rd_state, sizeof h->rd_state, &h->rd);
  return h;
}

static void
error_close (void *handle)
{
  struct handle *h = handle;

  free (h);
}

/* This function injects a random error. */
static bool
random_error (struct handle *h,
              const struct error_settings *error_settings,
              const char *fn, int *err)
{
  int32_t rand;

  if (error_settings->rate <= 0)       /* 0% = never inject */
    return false;

  /* Does the trigger file exist? */
  if (error_settings->file != NULL) {
    if (access (error_settings->file, F_OK) == -1)
      return false;
  }

  if (error_settings->rate >= 1)       /* 100% = always inject */
    goto inject;

  random_r (&h->rd, &rand);
  if (rand >= error_settings->rate * RAND_MAX)
    return false;

 inject:
  *err = error_settings->error;
  nbdkit_error ("injecting %s error into %s", error_as_string (*err), fn);
  return true;
}

/* Read data. */
static int
error_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle, void *buf, uint32_t count, uint64_t offset,
             uint32_t flags, int *err)
{
  if (random_error (handle, &pread_settings, "pread", err))
    return -1;

  return next_ops->pread (nxdata, buf, count, offset, flags, err);
}

/* Write data. */
static int
error_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
              void *handle,
              const void *buf, uint32_t count, uint64_t offset,
              uint32_t flags, int *err)
{
  if (random_error (handle, &pwrite_settings, "pwrite", err))
    return -1;

  return next_ops->pwrite (nxdata, buf, count, offset, flags, err);
}

/* Trim data. */
static int
error_trim (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle, uint32_t count, uint64_t offset,
            uint32_t flags, int *err)
{
  if (random_error (handle, &trim_settings, "trim", err))
    return -1;

  return next_ops->trim (nxdata, count, offset, flags, err);
}

/* Zero data. */
static int
error_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle, uint32_t count, uint64_t offset,
            uint32_t flags, int *err)
{
  if (random_error (handle, &zero_settings, "zero", err))
    return -1;

  return next_ops->zero (nxdata, count, offset, flags, err);
}

static struct nbdkit_filter filter = {
  .name              = "error",
  .longname          = "nbdkit error filter",
  .version           = PACKAGE_VERSION,
  .unload            = error_unload,
  .config            = error_config,
  .config_help       = error_config_help,
  .open              = error_open,
  .close             = error_close,
  .pread             = error_pread,
  .pwrite            = error_pwrite,
  .trim              = error_trim,
  .zero              = error_zero,
};

NBDKIT_REGISTER_FILTER(filter)
