/* nbdkit
 * Copyright (C) 2018-2019 Red Hat Inc.
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
#include <inttypes.h>
#include <string.h>

#include <pthread.h>

#if defined(HAVE_GNUTLS) && defined(HAVE_GNUTLS_BASE64_DECODE2)
#include <gnutls/gnutls.h>
#endif

#include <nbdkit-plugin.h>

#include "sparse.h"

/* If raw|base64|data parameter seen. */
static int data_seen = 0;

/* size= parameter on the command line, -1 if not set. */
static int64_t size = -1;

/* Size of data specified on the command line. */
static int64_t data_size = -1;

/* Sparse array - the lock must be held when accessing this from
 * connected callbacks.
 */
static struct sparse_array *sa;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* Debug directory operations (-D data.dir=1). */
int data_debug_dir;

static void
data_load (void)
{
  sa = alloc_sparse_array (data_debug_dir);
  if (sa == NULL) {
    perror ("malloc");
    exit (EXIT_FAILURE);
  }
}

/* On unload, free the sparse array. */
static void
data_unload (void)
{
  free_sparse_array (sa);
}

/* Parse the base64 parameter. */
static int
read_base64 (const char *value)
{
#if defined(HAVE_GNUTLS) && defined(HAVE_GNUTLS_BASE64_DECODE2)
  gnutls_datum_t in, out;
  int err;

  in.data = (unsigned char *) value;
  in.size = strlen (value);
  err = gnutls_base64_decode2 (&in, &out);
  if (err != GNUTLS_E_SUCCESS) {
    nbdkit_error ("base64: %s", gnutls_strerror (err));
    return -1;
  }

  if (sparse_array_write (sa, out.data, out.size, 0) == -1)
    return -1;
  free (out.data);
  return 0;
#else
  nbdkit_error ("base64 is not supported in this build of the plugin");
  return -1;
#endif
}

/* Store file at current offset in the sparse array, updating
 * the offset.
 */
static int
store_file (const char *filename, int64_t *offset)
{
  FILE *fp;
  char buf[BUFSIZ];
  size_t n;

  fp = fopen (filename, "r");
  if (fp == NULL) {
    nbdkit_error ("%s: %m", filename);
    return -1;
  }

  while (!feof (fp)) {
    n = fread (buf, 1, BUFSIZ, fp);
    if (n > 0) {
      if (sparse_array_write (sa, buf, n, *offset) == -1) {
        fclose (fp);
        return -1;
      }
    }
    if (ferror (fp)) {
      nbdkit_error ("fread: %s: %m", filename);
      fclose (fp);
      return -1;
    }
    (*offset) += n;
  }

  if (fclose (fp) == EOF) {
    nbdkit_error ("fclose: %s: %m", filename);
    return -1;
  }

  return 0;
}

/* Parse the data parameter. */
static int
read_data (const char *value)
{
  int64_t offset = 0;
  size_t i, len = strlen (value);

  for (i = 0; i < len; ++i) {
    int64_t j;
    int n;
    char c, cc[2];

    if (sscanf (&value[i], " @%" SCNi64 "%n", &j, &n) == 1) {
      if (j < 0) {
        nbdkit_error ("data parameter @OFFSET must not be negative");
        return -1;
      }
      i += n;
      offset = j;
    }
    /* We need %1s for obscure reasons.  sscanf " <%n" can return 0
     * if nothing is matched, not only if the '<' is matched.
     */
    else if (sscanf (&value[i], " <%1s%n", cc, &n) == 1) {
      char *filename;
      size_t flen;

      i += n-1;

      /* The filename follows next in the string. */
      flen = strcspn (&value[i], " \t\n");
      if (flen == 0) {
        nbdkit_error ("data parameter <FILE not a filename");
        return -1;
      }
      filename = strndup (&value[i], flen);
      if (filename == NULL) {
        nbdkit_error ("strndup: %m");
        return -1;
      }
      i += len;

      if (store_file (filename, &offset) == -1) {
        free (filename);
        return -1;
      }
      free (filename);

      if (data_size < offset)
        data_size = offset;
    }
    else if (sscanf (&value[i], " %" SCNi64 "%n", &j, &n) == 1) {
      if (j < 0 || j > 255) {
        nbdkit_error ("data parameter BYTE must be in the range 0..255");
        return -1;
      }
      i += n;

      if (data_size < offset+1)
        data_size = offset+1;

      /* Store the byte. */
      c = j;
      if (sparse_array_write (sa, &c, 1, offset) == -1)
        return -1;
      offset++;
    }
    /* We have to have a rule to skip just whitespace so that
     * whitespace is permitted at the end of the string.
     */
    else if (sscanf (&value[i], " %n", &n) == 0) {
      i += n;
    }
    else {
      nbdkit_error ("data parameter: parsing error at offset %zu", i);
      return -1;
    }
  }

  return 0;
}

static int
data_config (const char *key, const char *value)
{
  int64_t r;

  if (strcmp (key, "size") == 0) {
    r = nbdkit_parse_size (value);
    if (r == -1)
      return -1;
    size = r;
  }
  else if (strcmp (key, "raw") == 0 ||
           strcmp (key, "base64") == 0 ||
           strcmp (key, "data") == 0) {
    if (data_seen) {
      nbdkit_error ("raw|base64|data parameter must be specified exactly once");
      return -1;
    }
    data_seen = 1;

    if (strcmp (key, "raw") == 0) {
      data_size = strlen (value);
      if (sparse_array_write (sa, value, data_size, 0) == -1)
        return -1;
    }
    else if (strcmp (key, "base64") == 0) {
      if (read_base64 (value) == -1)
        return -1;
    }
    else if (strcmp (key, "data") == 0) {
      if (read_data (value) == -1)
        return -1;
    }
    else
      abort (); /* cannot happen */
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

/* Check the raw|base64|data was specified, and set the final size. */
static int
data_config_complete (void)
{
  if (!data_seen) {
    nbdkit_error ("raw|base64|data parameter was not specified");
    return -1;
  }

  nbdkit_debug ("implicit data size: %" PRIi64, data_size);

  /* If size == -1 it means the size= parameter was not given so we
   * must use the data size.
   */
  if (size == -1)
    size = data_size;
  nbdkit_debug ("final size: %" PRIi64, size);

  return 0;
}

#define data_config_help \
  "data|raw|base64=...     Specify disk data on the command line\n" \
  "size=<SIZE>  (required) Size of the backing disk"

/* Provide a way to detect if the base64 feature is supported. */
static void
data_dump_plugin (void)
{
#if defined(HAVE_GNUTLS) && defined(HAVE_GNUTLS_BASE64_DECODE2)
  printf ("data_base64=yes\n");
#endif
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Create the per-connection handle. */
static void *
data_open (int readonly)
{
  return NBDKIT_HANDLE_NOT_NEEDED;
}

/* Get the disk size. */
static int64_t
data_get_size (void *handle)
{
  return size;
}

/* Serves the same data over multiple connections. */
static int
data_can_multi_conn (void *handle)
{
  return 1;
}

/* Read data. */
static int
data_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  pthread_mutex_lock (&lock);
  sparse_array_read (sa, buf, count, offset);
  pthread_mutex_unlock (&lock);
  return 0;
}

/* Write data. */
static int
data_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset)
{
  int r;

  pthread_mutex_lock (&lock);
  r = sparse_array_write (sa, buf, count, offset);
  pthread_mutex_unlock (&lock);
  return r;
}

/* Zero. */
static int
data_zero (void *handle, uint32_t count, uint64_t offset, int may_trim)
{
  pthread_mutex_lock (&lock);
  sparse_array_zero (sa, count, offset);
  pthread_mutex_unlock (&lock);
  return 0;
}

/* Trim (same as zero). */
static int
data_trim (void *handle, uint32_t count, uint64_t offset)
{
  pthread_mutex_lock (&lock);
  sparse_array_zero (sa, count, offset);
  pthread_mutex_unlock (&lock);
  return 0;
}

/* Extents. */
static int
data_extents (void *handle, uint32_t count, uint64_t offset,
              uint32_t flags, struct nbdkit_extents *extents)
{
  int r;

  pthread_mutex_lock (&lock);
  r = sparse_array_extents (sa, count, offset, extents);
  pthread_mutex_unlock (&lock);
  return r;
}

static struct nbdkit_plugin plugin = {
  .name              = "data",
  .version           = PACKAGE_VERSION,
  .load              = data_load,
  .unload            = data_unload,
  .config            = data_config,
  .config_complete   = data_config_complete,
  .config_help       = data_config_help,
  .dump_plugin       = data_dump_plugin,
  .open              = data_open,
  .get_size          = data_get_size,
  .can_multi_conn    = data_can_multi_conn,
  .pread             = data_pread,
  .pwrite            = data_pwrite,
  .zero              = data_zero,
  .trim              = data_trim,
  .extents           = data_extents,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
