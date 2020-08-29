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

/* This is the Windows version of the file plugin. */

#ifndef WIN32
#error "build error: file.c should be used on Unix-like platforms"
#endif

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include <ws2tcpip.h>
#include <windows.h>

#include <pthread.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

static char *filename = NULL;

static void
winfile_unload (void)
{
  free (filename);
}

static int
winfile_config (const char *key, const char *value)
{
  if (strcmp (key, "file") == 0) {
    free (filename);
    filename = nbdkit_realpath (value);
    if (!filename)
      return -1;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

/* Check the user passed the file parameter. */
static int
winfile_config_complete (void)
{
  if (!filename) {
    nbdkit_error ("you must supply either [file=]<FILENAME> parameter "
                  "after the plugin name on the command line");
    return -1;
  }

  return 0;
}

#define winfile_config_help \
  "[file=]<FILENAME>     The filename to serve."

/* Print some extra information about how the plugin was compiled. */
static void
winfile_dump_plugin (void)
{
  printf ("winfile=yes\n");
}

/* Per-connection handle. */
struct handle {
  HANDLE fh;
  int64_t size;
  bool is_volume;
};

static void *
winfile_open (int readonly)
{
  struct handle *h;
  HANDLE fh;
  LARGE_INTEGER size = { 0 };
  DWORD flags;
  bool is_volume;

  flags = GENERIC_READ;
  if (!readonly) flags |= GENERIC_WRITE;

  fh = CreateFile (filename, flags, FILE_SHARE_READ|FILE_SHARE_WRITE,
                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (fh == INVALID_HANDLE_VALUE) {
    nbdkit_error ("%s: error %lu", filename, GetLastError ());
    return NULL;
  }

  /* https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file#win32-device-namespaces */
  is_volume = strncmp (filename, "\\\\.\\", 4) == 0;

  if (is_volume) {
    /* Windows volume (block device).  Get the size. */
    GET_LENGTH_INFORMATION li;
    DWORD lisz = sizeof li;
    DWORD obsz = 0;

    if (!DeviceIoControl (fh, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                          (LPVOID) &li, lisz, &obsz, NULL)) {
      nbdkit_error ("%s: DeviceIoControl: %lu", filename, GetLastError ());
      CloseHandle (fh);
      return NULL;
    }
    size.QuadPart = li.Length.QuadPart;
  }
  else {
    /* Regular file.  Get the size. */
    if (!GetFileSizeEx (fh, &size)) {
      nbdkit_error ("%s: GetFileSizeEx: %lu", filename, GetLastError ());
      CloseHandle (fh);
      return NULL;
    }
  }

  h = malloc (sizeof *h);
  if (!h) {
    nbdkit_error ("malloc: %lu", GetLastError ());
    CloseHandle (fh);
    return NULL;
  }
  h->fh = fh;
  h->size = size.QuadPart;
  h->is_volume = is_volume;
  nbdkit_debug ("%s: size=%" PRIi64 " is_volume=%s",
                filename, h->size, is_volume ? "true" : "false");
  return h;
}

static void
winfile_close (void *handle)
{
  struct handle *h = handle;
  CloseHandle (h->fh);
}

static int64_t
winfile_get_size (void *handle)
{
  struct handle *h = handle;
  return h->size;
}

static int
winfile_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
               uint32_t flags)
{
  struct handle *h = handle;
  DWORD r;
  OVERLAPPED ovl;

  memset (&ovl, 0, sizeof ovl);
  ovl.Offset = offset & 0xffffffff;
  ovl.OffsetHigh = offset >> 32;

  /* XXX Will fail weirdly if count is larger than 32 bits. */
  if (!ReadFile (h->fh, buf, count, &r, &ovl)) {
    nbdkit_error ("%s: ReadFile: %lu", filename, GetLastError ());
    return -1;
  }
  return 0;
}

static int
winfile_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
                uint32_t flags)
{
  struct handle *h = handle;
  DWORD r;
  OVERLAPPED ovl;

  memset (&ovl, 0, sizeof ovl);
  ovl.Offset = offset & 0xffffffff;
  ovl.OffsetHigh = offset >> 32;

  /* XXX Will fail weirdly if count is larger than 32 bits. */
  if (!WriteFile (h->fh, buf, count, &r, &ovl)) {
    nbdkit_error ("%s: WriteFile: %lu", filename, GetLastError ());
    return -1;
  }

  if (flags & NBDKIT_FLAG_FUA) {
    if (!FlushFileBuffers (h->fh)) {
      nbdkit_error ("%s: FlushFileBuffers: %lu", filename, GetLastError ());
      return -1;
    }
  }

  return 0;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

static struct nbdkit_plugin plugin = {
  .name              = "file",
  .longname          = "nbdkit file plugin (Windows)",
  .version           = PACKAGE_VERSION,

  .unload            = winfile_unload,

  .config            = winfile_config,
  .config_complete   = winfile_config_complete,
  .config_help       = winfile_config_help,
  .magic_config_key  = "file",
  .dump_plugin       = winfile_dump_plugin,

  .open              = winfile_open,
  .close             = winfile_close,
  .get_size          = winfile_get_size,
  .pread             = winfile_pread,
  .pwrite            = winfile_pwrite,

  .errno_is_preserved = 1, /* XXX ? */
};

NBDKIT_REGISTER_PLUGIN(plugin)
