/* nbdkit
 * Copyright (C) 2013-2018 Red Hat Inc.
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

/* Types and structs that we pass to or return from the VDDK API.
 *
 * Updated to VDDK 6.7
 */

#ifndef NBDKIT_VDDK_STRUCTS_H
#define NBDKIT_VDDK_STRUCTS_H

typedef uint64_t VixError;
#define VIX_OK 0

#define VIXDISKLIB_FLAG_OPEN_READ_ONLY 4
#define VIXDISKLIB_SECTOR_SIZE 512

typedef void *VixDiskLibConnection;
typedef void *VixDiskLibHandle;

typedef void VixDiskLibGenericLogFunc (const char *fmt, va_list args);

enum VixDiskLibCredType {
  VIXDISKLIB_CRED_UID       = 1,
  VIXDISKLIB_CRED_SESSIONID = 2,
  VIXDISKLIB_CRED_TICKETID  = 3,
  VIXDISKLIB_CRED_SSPI      = 4,
  VIXDISKLIB_CRED_UNKNOWN   = 256
};

enum VixDiskLibSpecType {
  VIXDISKLIB_SPEC_VMX             = 0,
  VIXDISKLIB_SPEC_VSTORAGE_OBJECT = 1,
  VIXDISKLIB_SPEC_UNKNOWN         = 2
};

struct VixDiskLibVStorageObjectSpec {
  char *id;
  char *datastoreMoRef;
  char *ssId;
};

typedef struct VixDiskLibConnectParams {
  char *vmxSpec;
  char *serverName;
  char *thumbPrint;
  long reserved1;
  enum VixDiskLibCredType credType;
  union {
    struct {
      char *userName;
      char *password;
    } uid;
    struct {
      char *cookie;
      char *userName;
      char *key;
    } sessionId;
    void *reserved2;
  } creds;
  uint32_t port;
  uint32_t nfcHostPort;
  char *reserved3;
  char reserved4[8];
  void *reserved5;
  union {
    struct VixDiskLibVStorageObjectSpec vStorageObjSpec;
  } spec;
  enum VixDiskLibSpecType specType;
} VixDiskLibConnectParams;

struct VixDiskLibGeometry {
  uint32_t cylinders;
  uint32_t heads;
  uint32_t sectors;
};

enum VixDiskLibAdapterType {
  VIXDISKLIB_ADAPTER_IDE           = 1,
  VIXDISKLIB_ADAPTER_SCSI_BUSLOGIC = 2,
  VIXDISKLIB_ADAPTER_SCSI_LSILOGIC = 3,
  VIXDISKLIB_ADAPTER_UNKNOWN       = 256
};

typedef struct VixDiskLibInfo {
  struct VixDiskLibGeometry biosGeo;
  struct VixDiskLibGeometry physGeo;
  uint64_t capacity;
  enum VixDiskLibAdapterType adapterType;
  int numLinks;
  char *parentFileNameHint;
  char *uuid;
} VixDiskLibInfo;

#endif /* NBDKIT_VDDK_STRUCTS_H */
