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

/* Types and structs that we pass to or return from the VDDK API.
 *
 * Updated to VDDK 7.0
 */

#ifndef NBDKIT_VDDK_STRUCTS_H
#define NBDKIT_VDDK_STRUCTS_H

#include <stdarg.h>
#include <stdint.h>

typedef uint64_t VixError;
#define VIX_OK 0
#define VIX_E_FAIL 1
#define VIX_E_NOT_SUPPORTED 6
#define VIX_ASYNC 25000

#define VIXDISKLIB_FLAG_OPEN_UNBUFFERED 1
#define VIXDISKLIB_FLAG_OPEN_SINGLE_LINK 2
#define VIXDISKLIB_FLAG_OPEN_READ_ONLY 4
#define VIXDISKLIB_FLAG_OPEN_COMPRESSION_ZLIB 16
#define VIXDISKLIB_FLAG_OPEN_COMPRESSION_FASTLZ 32
#define VIXDISKLIB_FLAG_OPEN_COMPRESSION_SKIPZ 64

#define VIXDISKLIB_SECTOR_SIZE 512

enum VixDiskLibDiskType {
  VIXDISKLIB_DISK_MONOLITHIC_SPARSE = 1,
  VIXDISKLIB_DISK_MONOLITHIC_FLAT = 2,
  VIXDISKLIB_DISK_SPLIT_SPARSE = 3,
  VIXDISKLIB_DISK_SPLIT_FLAT = 4,
  VIXDISKLIB_DISK_VMFS_FLAT = 5,
  VIXDISKLIB_DISK_STREAM_OPTIMIZED = 6,
  VIXDISKLIB_DISK_VMFS_THIN = 7,
  VIXDISKLIB_DISK_VMFS_SPARSE = 8
};

#define VIXDISKLIB_HWVERSION_WORKSTATION_4 3
#define VIXDISKLIB_HWVERSION_WORKSTATION_5 4
#define VIXDISKLIB_HWVERSION_WORKSTATION_6 6
#define VIXDISKLIB_HWVERSION_ESX30 4
#define VIXDISKLIB_HWVERSION_ESX4X 7
#define VIXDISKLIB_HWVERSION_ESX50 8
#define VIXDISKLIB_HWVERSION_ESX51 9
#define VIXDISKLIB_HWVERSION_ESX55 10
#define VIXDISKLIB_HWVERSION_ESX60 11
#define VIXDISKLIB_HWVERSION_ESX65 13

#define VIXDISKLIB_MIN_CHUNK_SIZE 128
#define VIXDISKLIB_MAX_CHUNK_NUMBER (512*1024)

typedef void *VixDiskLibConnection;
typedef void *VixDiskLibHandle;

typedef void VixDiskLibGenericLogFunc (const char *fmt, va_list args);

typedef void (*VixDiskLibCompletionCB) (void *data, VixError result);

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
  uint32_t logicalSectorSize;   /* Added in 7.0. */
  uint32_t physicalSectorSize;  /* Added in 7.0. */
} VixDiskLibInfo;

typedef struct {
  uint64_t offset;
  uint64_t length;
} VixDiskLibBlock;

typedef struct {
  uint32_t numBlocks;
  VixDiskLibBlock blocks[1];
} VixDiskLibBlockList;

typedef struct {
  enum VixDiskLibDiskType diskType;
  enum VixDiskLibAdapterType adapterType;
  uint16_t hwVersion;
  uint64_t capacity;
  uint32_t logicalSectorSize;
  uint32_t physicalSectorSize;
} VixDiskLibCreateParams;

#endif /* NBDKIT_VDDK_STRUCTS_H */
