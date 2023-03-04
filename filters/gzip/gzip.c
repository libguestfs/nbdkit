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
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include <zlib.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "pread.h"
#include "minmax.h"

/* The first thread to call gzip_prepare has to uncompress the whole
 * plugin to the temporary file.  This lock prevents concurrent
 * access.
 */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* Temporary file storing the uncompressed data. */
static int fd = -1;

/* Size of compressed and uncompressed data. */
static int64_t compressed_size = -1, size = -1;

static void
gzip_unload (void)
{
  if (fd >= 0)
    close (fd);
}

static int
gzip_thread_model (void)
{
  return NBDKIT_THREAD_MODEL_PARALLEL;
}

static void *
gzip_open (nbdkit_next_open *next, nbdkit_context *nxdata,
           int readonly, const char *exportname, int is_tls)
{
  /* Always pass readonly=1 to the underlying plugin. */
  if (next (nxdata, 1, exportname) == -1)
    return NULL;

  return NBDKIT_HANDLE_NOT_NEEDED;
}

/* Convert a zlib error (always negative) to an nbdkit error message,
 * and return errno correctly.
 */
static void
zerror (const char *op, const z_stream *strm, int zerr)
{
  if (zerr == Z_MEM_ERROR) {
    errno = ENOMEM;
    nbdkit_error ("gzip: %s: %m", op);
  }
  else {
    errno = EIO;
    if (strm->msg)
      nbdkit_error ("gzip: %s: %s", op, strm->msg);
    else
      nbdkit_error ("gzip: %s: unknown error: %d", op, zerr);
  }
}

/* Write a whole buffer to the temporary file or fail. */
static int
xwrite (const void *buf, size_t count)
{
  ssize_t r;

  while (count > 0) {
    r = write (fd, buf, count);
    if (r == -1) {
      nbdkit_error ("write: %m");
      return -1;
    }
    buf += r;
    count -= r;
  }

  return 0;
}

/* The first thread to call gzip_prepare uncompresses the whole plugin. */
static int
do_uncompress (nbdkit_next *next)
{
  z_stream strm;
  int zerr;
  const char *tmpdir;
  size_t len;
  char *template;
  CLEANUP_FREE char *in_block = NULL, *out_block = NULL;

  /* Choose a generous block size here because it's more efficient
   * with some plugins (esp. curl).  XXX This should really be
   * configurable.
   */
  const size_t block_size = 4 * 1024 * 1024;

  assert (size == -1);

  /* Get the size of the underlying plugin. */
  compressed_size = next->get_size (next);
  if (compressed_size == -1)
    return -1;

  /* Create the temporary file. */
  tmpdir = getenv ("TMPDIR");
  if (!tmpdir)
    tmpdir = LARGE_TMPDIR;

  len = strlen (tmpdir) + 8;
  template = alloca (len);
  snprintf (template, len, "%s/XXXXXX", tmpdir);

#ifdef HAVE_MKOSTEMP
  fd = mkostemp (template, O_CLOEXEC);
#else
  /* This is only invoked serially with the lock held, so this is safe. */
  fd = mkstemp (template);
#ifndef WIN32
  if (fd >= 0) {
    fd = set_cloexec (fd);
    if (fd < 0) {
      int e = errno;
      unlink (template);
      errno = e;
    }
  }
#endif /* WIN32 */
#endif /* !HAVE_MKOSTEMP */
  if (fd == -1) {
    nbdkit_error ("mkostemp: %s: %m", tmpdir);
    return -1;
  }

  unlink (template);

  /* Uncompress the whole plugin.  This is REQUIRED in order to
   * implement gzip_get_size.  See: https://stackoverflow.com/a/9213826
   *
   * For use of inflateInit2 on gzip streams see:
   * https://stackoverflow.com/a/1838702
   */
  memset (&strm, 0, sizeof strm);
  zerr = inflateInit2 (&strm, 16+MAX_WBITS);
  if (zerr != Z_OK) {
    zerror ("inflateInit2", &strm, zerr);
    return -1;
  }

  in_block = malloc (block_size);
  if (!in_block) {
    nbdkit_error ("malloc: %m");
    return -1;
  }
  out_block = malloc (block_size);
  if (!out_block) {
    nbdkit_error ("malloc: %m");
    return -1;
  }

  for (;;) {
    /* Do we need to read more from the plugin? */
    if (strm.avail_in == 0 && strm.total_in < compressed_size) {
      size_t n = MIN (block_size, compressed_size - strm.total_in);
      int err = 0;

      if (next->pread (next, in_block, (uint32_t)n, strm.total_in, 0,
                       &err) == -1) {
        errno = err;
        return -1;
      }

      strm.next_in = (void *) in_block;
      strm.avail_in = n;
    }

    /* Inflate the next chunk of input. */
    strm.next_out = (void *) out_block;
    strm.avail_out = block_size;
    zerr = inflate (&strm, Z_SYNC_FLUSH);
    if (zerr < 0) {
      zerror ("inflate", &strm, zerr);
      return -1;
    }

    /* Write the output to the file. */
    if (xwrite (out_block, (char *) strm.next_out - out_block) == -1)
      return -1;

    if (zerr == Z_STREAM_END)
      break;
  }

  /* Set the size to the total uncompressed size. */
  size = strm.total_out;
  nbdkit_debug ("gzip: uncompressed size: %" PRIi64, size);

  zerr = inflateEnd (&strm);
  if (zerr != Z_OK) {
    zerror ("inflateEnd", &strm, zerr);
    return -1;
  }

  return 0;
}

static int
gzip_prepare (nbdkit_next *next, void *handle,
              int readonly)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);

  if (size >= 0)
    return 0;
  return do_uncompress (next);
}

/* Whatever the plugin says, this filter makes it read-only. */
static int
gzip_can_write (nbdkit_next *next,
                void *handle)
{
  return 0;
}

/* Whatever the plugin says, this filter is consistent across connections. */
static int
gzip_can_multi_conn (nbdkit_next *next,
                     void *handle)
{
  return 1;
}

/* Similar to above, whatever the plugin says, extents are not
 * supported.
 */
static int
gzip_can_extents (nbdkit_next *next,
                  void *handle)
{
  return 0;
}

/* We are already operating as a cache regardless of the plugin's
 * underlying .can_cache, but it's easiest to just rely on nbdkit's
 * behavior of calling .pread for caching.
 */
static int
gzip_can_cache (nbdkit_next *next,
                void *handle)
{
  return NBDKIT_CACHE_EMULATE;
}

/* Description. */
static const char *
gzip_export_description (nbdkit_next *next,
                         void *handle)
{
  const char *base = next->export_description (next);

  if (!base)
    return NULL;
  return nbdkit_printf_intern ("expansion of gzip-compressed image: %s", base);
}

/* Get the file size. */
static int64_t
gzip_get_size (nbdkit_next *next,
               void *handle)
{
  int64_t t;

  /* This must be true because gzip_prepare must have been called. */
  assert (size >= 0);

  /* Check the plugin size didn't change underneath us. */
  t = next->get_size (next);
  if (t == -1)
    return -1;
  if (t != compressed_size) {
    nbdkit_error ("plugin size changed unexpectedly: "
                  "you must restart nbdkit so the gzip filter "
                  "can uncompress the data again");
    return -1;
  }

  return size;
}

/* Read data from the temporary file. */
static int
gzip_pread (nbdkit_next *next,
            void *handle, void *buf, uint32_t count, uint64_t offset,
            uint32_t flags, int *err)
{
  /* This must be true because gzip_prepare must have been called. */
  assert (fd >= 0);

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

static struct nbdkit_filter filter = {
  .name               = "gzip",
  .longname           = "nbdkit gzip filter",
  .unload             = gzip_unload,
  .thread_model       = gzip_thread_model,
  .open               = gzip_open,
  .prepare            = gzip_prepare,
  .can_write          = gzip_can_write,
  .can_extents        = gzip_can_extents,
  .can_cache          = gzip_can_cache,
  .can_multi_conn     = gzip_can_multi_conn,
  .export_description = gzip_export_description,
  .get_size           = gzip_get_size,
  .pread              = gzip_pread,
};

NBDKIT_REGISTER_FILTER (filter)
