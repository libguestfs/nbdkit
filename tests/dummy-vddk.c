/* nbdkit
 * Copyright (C) 2018 Red Hat Inc.
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

/* This file pretends to be libvixDiskLib.so.6. */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <pthread.h>

#include "nbdkit-plugin.h" /* only for NBDKIT_DLL_PUBLIC */

#include "vddk-structs.h"

#define STUB(fn,ret,args) extern ret fn args;
#define OPTIONAL_STUB(fn,ret,args)
#include "vddk-stubs.h"
#undef STUB
#undef OPTIONAL_STUB

#define CAPACITY 1024 /* sectors */
static char disk[CAPACITY * VIXDISKLIB_SECTOR_SIZE];

static pthread_t thread;

static void *
bg_thread (void *datav)
{
  for (;;)
    pause ();
  /*NOTREACHED*/
  return NULL;
}

NBDKIT_DLL_PUBLIC VixError
VixDiskLib_InitEx (uint32_t major, uint32_t minor,
                   VixDiskLibGenericLogFunc *log_function,
                   VixDiskLibGenericLogFunc *warn_function,
                   VixDiskLibGenericLogFunc *panic_function,
                   const char *lib_dir, const char *config_file)
{
  int err;

  /* Real VDDK creates one or more background threads, and this caused
   * problems in the past when we forked stranding those threads.
   * Create a background thread, and we will check that it still
   * exists when reading later.
   */
  err = pthread_create (&thread, NULL, bg_thread, NULL);
  if (err) {
    errno = err;
    perror ("pthread_create");
    abort ();
  }

  return VIX_OK;
}

NBDKIT_DLL_PUBLIC void
VixDiskLib_Exit (void)
{
  /* Do nothing. */
}

NBDKIT_DLL_PUBLIC char *
VixDiskLib_GetErrorText (VixError err, const char *unused)
{
  return strdup ("dummy-vddk: error message");
}

NBDKIT_DLL_PUBLIC void
VixDiskLib_FreeErrorText (char *text)
{
  free (text);
}

NBDKIT_DLL_PUBLIC void
VixDiskLib_FreeConnectParams (VixDiskLibConnectParams *params)
{
  /* never called since we don't define optional AllocateConnectParams */
  abort ();
}

NBDKIT_DLL_PUBLIC VixError
VixDiskLib_ConnectEx (const VixDiskLibConnectParams *params,
                      char read_only,
                      const char *snapshot_ref,
                      const char *transport_modes,
                      VixDiskLibConnection *connection)
{
  /* Used when regression testing password= parameter. */
  if (getenv ("DUMMY_VDDK_PRINT_PASSWORD") &&
      params->credType == VIXDISKLIB_CRED_UID &&
      params->creds.uid.password != NULL)
    fprintf (stderr, "dummy-vddk: password=%s\n", params->creds.uid.password);

  return VIX_OK;
}

NBDKIT_DLL_PUBLIC VixError
VixDiskLib_Open (const VixDiskLibConnection connection,
                 const char *path,
                 uint32_t flags,
                 VixDiskLibHandle *handle)
{
  /* Check that the background thread is still present. */
  if (pthread_kill (thread, 0) != 0)
    abort ();

  return VIX_OK;
}

NBDKIT_DLL_PUBLIC const char *
VixDiskLib_GetTransportMode (VixDiskLibHandle handle)
{
  return "file";
}

NBDKIT_DLL_PUBLIC VixError
VixDiskLib_Close (VixDiskLibHandle handle)
{
  return VIX_OK;
}

NBDKIT_DLL_PUBLIC VixError
VixDiskLib_Disconnect (VixDiskLibConnection connection)
{
  return VIX_OK;
}

NBDKIT_DLL_PUBLIC VixError
VixDiskLib_GetInfo (VixDiskLibHandle handle,
                    VixDiskLibInfo **info)
{
  *info = calloc (1, sizeof (struct VixDiskLibInfo));
  (*info)->capacity = CAPACITY;
  return VIX_OK;
}

NBDKIT_DLL_PUBLIC void
VixDiskLib_FreeInfo (VixDiskLibInfo *info)
{
  free (info);
}

NBDKIT_DLL_PUBLIC VixError
VixDiskLib_Read (VixDiskLibHandle handle,
                 uint64_t start_sector, uint64_t nr_sectors,
                 unsigned char *buf)
{
  size_t offset = start_sector * VIXDISKLIB_SECTOR_SIZE;

  memcpy (buf, disk + offset, nr_sectors * VIXDISKLIB_SECTOR_SIZE);
  return VIX_OK;
}

NBDKIT_DLL_PUBLIC VixError
VixDiskLib_Write (VixDiskLibHandle handle,
                  uint64_t start_sector, uint64_t nr_sectors,
                  const unsigned char *buf)
{
  size_t offset = start_sector * VIXDISKLIB_SECTOR_SIZE;

  memcpy (disk + offset, buf, nr_sectors * VIXDISKLIB_SECTOR_SIZE);
  return VIX_OK;
}

NBDKIT_DLL_PUBLIC VixError
VixDiskLib_Wait (VixDiskLibHandle handle)
{
  return VIX_OK;
}
