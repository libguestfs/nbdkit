/* nbdkit
 * Copyright (C) 2014 Red Hat Inc.
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
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <nbdkit-plugin.h>

/* Mode - read or write? */
static enum { UNKNOWN_MODE, READ_MODE, WRITE_MODE } mode = UNKNOWN_MODE;

/* The pipe. */
static char *filename = NULL;
static int fd = -1;

/* This is 2^63 - 2^30.  This is the largest disk that qemu supports. */
static int64_t size = INT64_C(9223372035781033984);

/* Flag if we have entered the unrecoverable error state because of
 * a seek backwards.
 */
static bool errorstate = 0;

/* Highest byte (+1) that has been accessed in the data stream. */
static uint64_t highest = 0;

static void
streaming_unload (void)
{
  if (fd >= 0)
    close (fd);
  free (filename);
}

/* Called for each key=value passed on the command line. */
static int
streaming_config (const char *key, const char *value)
{
  if (strcmp (key, "write") == 0 ||
      strcmp (key, "pipe") == 0) {
    if (mode != UNKNOWN_MODE) {
      nbdkit_error ("you cannot use read and write options at the same time");
      return -1;
    }
    mode = WRITE_MODE;
    goto adjust_filename;
  }
  else if (strcmp (key, "read") == 0) {
    if (mode != UNKNOWN_MODE) {
      nbdkit_error ("you cannot use read and write options at the same time");
      return -1;
    }
    mode = READ_MODE;
  adjust_filename:
    filename = nbdkit_absolute_path (value);
    if (!filename)
      return -1;
  }
  else if (strcmp (key, "size") == 0) {
    size = nbdkit_parse_size (value);
    if (size == -1)
      return -1;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

/* Did the user pass either the read or write parameter? */
static int
streaming_config_complete (void)
{
  if (mode == UNKNOWN_MODE) {
    nbdkit_error ("you must supply either the read=<FILENAME> or "
                  "write=<FILENAME> parameter "
                  "after the plugin name on the command line");
    return -1;
  }

  return 0;
}

#define streaming_config_help \
  "read=<FILENAME>                The pipe or socket to read.\n" \
  "write=<FILENAME>               The pipe or socket to write.\n" \
  "size=<SIZE>         (optional) Stream size."

static int
streaming_get_ready (void)
{
  int flags;

  assert (mode != UNKNOWN_MODE);
  assert (filename != NULL);
  assert (fd == -1);

  flags = O_CLOEXEC|O_NOCTTY;
  if (mode == WRITE_MODE)
    flags |= O_RDWR;
  else
    flags |= O_RDONLY;

  /* Open the file blindly.  If this fails with ENOENT then we create a
   * FIFO and try again.
   */
 again:
  fd = open (filename, flags);
  if (fd == -1) {
    if (errno != ENOENT) {
      nbdkit_error ("open: %s: %m", filename);
      return -1;
    }
    if (mknod (filename, S_IFIFO | 0666, 0) == -1) {
      nbdkit_error ("mknod: %s: %m", filename);
      return -1;
    }
    goto again;
  }

  return 0;
}

/* Create the per-connection handle. */
static void *
streaming_open (int readonly)
{
  if (readonly) {
    nbdkit_error ("you cannot use the -r option with the streaming plugin");
    return NULL;
  }

  if (errorstate) {
    nbdkit_error ("unrecoverable error state, "
                  "no new connections can be opened");
    return NULL;
  }

  return NBDKIT_HANDLE_NOT_NEEDED;
}

/* In write mode, writes are allowed.  In read mode, we act as if -r
 * was passed on the command line and the client will not be allowed
 * to write.
 */
static int
streaming_can_write (void *h)
{
  return mode == WRITE_MODE;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

/* Return the size of the stream (infinite). */
static int64_t
streaming_get_size (void *handle)
{
  return size;
}

/* Read data back from the stream. */
static int
streaming_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  size_t n;
  ssize_t r;

  if (errorstate) {
    nbdkit_error ("unrecoverable error state");
    errno = EIO;
    return -1;
  }

  if (mode == READ_MODE) {
    if (offset < highest) {
      nbdkit_error ("client tried to seek backwards and read: "
                    "the streaming plugin does not support this");
      errorstate = true;
      errno = EIO;
      return -1;
    }

    /* If the offset is higher than previously read we must seek
     * forwards and discard data.
     */
    if (offset > highest) {
      int64_t remaining = offset - highest;
      static char discard[4096];

      while (remaining > 0) {
        n = remaining > sizeof discard ? sizeof discard : remaining;
        r = read (fd, discard, n);
        if (r == -1) {
          nbdkit_error ("read: %m");
          errorstate = true;
          return -1;
        }
        if (r == 0) {
          nbdkit_error ("read: unexpected end of file reading from the pipe");
          errorstate = true;
          return -1;
        }
        highest += r;
        remaining -= r;
      }
    }

    /* Read data from the pipe into the return buffer. */
    while (count > 0) {
      r = read (fd, buf, count);
      if (r == -1) {
        nbdkit_error ("read: %m");
        errorstate = true;
        return -1;
      }
      if (r == 0) {
        nbdkit_error ("read: unexpected end of file reading from the pipe");
        errorstate = true;
        return -1;
      }
      buf += r;
      highest += r;
      count -= r;
    }

    return 0;
  }

  /* WRITE_MODE */
  else {
    /* Allow reads which are entirely >= highest.  These return zeroes. */
    if (offset >= highest) {
      memset (buf, 0, count);
      return 0;
    }

    nbdkit_error ("client tried to read, but the streaming plugin is "
                  "being used in write mode (write= parameter)");
    errorstate = true;
    errno = EIO;
    return -1;
  }
}

/* Write data to the stream. */
static int
streaming_pwrite (void *handle, const void *buf,
                  uint32_t count, uint64_t offset)
{
  size_t n;
  ssize_t r;

  /* This can never happen because streaming_can_write above returns
   * false in read mode.
   */
  assert (mode == WRITE_MODE);

  if (errorstate) {
    nbdkit_error ("unrecoverable error state");
    errno = EIO;
    return -1;
  }

  if (offset < highest) {
    nbdkit_error ("client tried to seek backwards and write: "
                  "the streaming plugin does not support this");
    errorstate = true;
    errno = EIO;
    return -1;
  }

  /* Need to write some zeroes. */
  if (offset > highest) {
    int64_t remaining = offset - highest;
    static char zerobuf[4096];

    while (remaining > 0) {
      n = remaining > sizeof zerobuf ? sizeof zerobuf : remaining;
      r = write (fd, zerobuf, n);
      if (r == -1) {
        nbdkit_error ("write: %m");
        errorstate = true;
        return -1;
      }
      highest += r;
      remaining -= r;
    }
  }

  /* Write the data. */
  while (count > 0) {
    r = write (fd, buf, count);
    if (r == -1) {
      nbdkit_error ("write: %m");
      errorstate = true;
      return -1;
    }
    buf += r;
    highest += r;
    count -= r;
  }

  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "streaming",
  .longname          = "nbdkit streaming plugin",
  .version           = PACKAGE_VERSION,
  .unload            = streaming_unload,
  .config            = streaming_config,
  .config_complete   = streaming_config_complete,
  .config_help       = streaming_config_help,
  .get_ready         = streaming_get_ready,
  .open              = streaming_open,
  .can_write         = streaming_can_write,
  .get_size          = streaming_get_size,
  .pread             = streaming_pread,
  .pwrite            = streaming_pwrite,
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
