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

/* This header file lists the functions in VDDK that we will try to
 * dlsym.  It is included in several places in the main plugin, each
 * time with STUB and OPTIONAL_STUB macros expanding to different
 * things depending on the context.
 *
 * The three parameters of STUB and OPTIONAL_STUB macros are always:
 * function name, return value, arguments.
 */

/* Required stubs, present in all versions of VDDK that we support.  I
 * have checked that all these exist in at least VDDK 5.5.5 (2015).
 */

STUB (VixDiskLib_InitEx,
      VixError,
      (uint32_t major, uint32_t minor,
       VixDiskLibGenericLogFunc *log_function,
       VixDiskLibGenericLogFunc *warn_function,
       VixDiskLibGenericLogFunc *panic_function,
       const char *lib_dir,
       const char *config_file));
STUB (VixDiskLib_Exit,
      void,
      (void));
STUB (VixDiskLib_GetErrorText,
      char *,
      (VixError err, const char *unused));
STUB (VixDiskLib_FreeErrorText,
      void,
      (char *text));
STUB (VixDiskLib_FreeConnectParams,
      void,
      (VixDiskLibConnectParams *params));
STUB (VixDiskLib_ConnectEx,
      VixError,
      (const VixDiskLibConnectParams *params,
       char read_only,
       const char *snapshot_ref,
       const char *transport_modes,
       VixDiskLibConnection *connection));
STUB (VixDiskLib_Open,
      VixError,
      (const VixDiskLibConnection connection,
       const char *path,
       uint32_t flags,
       VixDiskLibHandle *handle));
STUB (VixDiskLib_GetTransportMode,
      const char *,
      (VixDiskLibHandle handle));
STUB (VixDiskLib_Close,
      VixError,
      (VixDiskLibHandle handle));
STUB (VixDiskLib_Disconnect,
      VixError,
      (VixDiskLibConnection connection));
STUB (VixDiskLib_GetInfo,
      VixError,
      (VixDiskLibHandle handle,
       VixDiskLibInfo **info));
STUB (VixDiskLib_FreeInfo,
      void,
      (VixDiskLibInfo *info));
STUB (VixDiskLib_Read,
      VixError,
      (VixDiskLibHandle handle,
       uint64_t start_sector, uint64_t nr_sectors,
       unsigned char *buf));
STUB (VixDiskLib_Write,
      VixError,
      (VixDiskLibHandle handle,
       uint64_t start_sector, uint64_t nr_sectors,
       const unsigned char *buf));

/* Added in VDDK 6.0, these will be NULL in earlier versions. */
OPTIONAL_STUB (VixDiskLib_Flush,
               VixError,
               (VixDiskLibHandle handle));
OPTIONAL_STUB (VixDiskLib_ReadAsync,
               VixError,
               (VixDiskLibHandle handle,
                uint64_t start_sector, uint64_t nr_sectors,
                unsigned char *buf,
                VixDiskLibCompletionCB callback, void *data));
OPTIONAL_STUB (VixDiskLib_WriteAsync,
               VixError,
               (VixDiskLibHandle handle,
                uint64_t start_sector, uint64_t nr_sectors,
                const unsigned char *buf,
                VixDiskLibCompletionCB callback, void *data));

/* Added in VDDK 6.5, this will be NULL in earlier versions. */
OPTIONAL_STUB (VixDiskLib_Wait,
               VixError,
               (VixDiskLibHandle handle));

/* Added in VDDK 6.7, these will be NULL for earlier versions: */
OPTIONAL_STUB (VixDiskLib_QueryAllocatedBlocks,
               VixError,
               (VixDiskLibHandle diskHandle,
                uint64_t start_sector, uint64_t nr_sectors,
                uint64_t chunk_size,
                VixDiskLibBlockList **block_list));
OPTIONAL_STUB (VixDiskLib_FreeBlockList,
               VixError,
               (VixDiskLibBlockList *block_list));
OPTIONAL_STUB (VixDiskLib_AllocateConnectParams,
               VixDiskLibConnectParams *,
               (void));
