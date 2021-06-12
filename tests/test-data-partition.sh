#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018-2021 Red Hat Inc.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution.
#
# * Neither the name of Red Hat nor the names of its contributors may be
# used to endorse or promote products derived from this software without
# specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
# PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
# USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
# OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

# Test the MBR-formatted partition example from the manual.

source ./functions.sh
set -e
set -x

requires_nbdsh_uri
requires nbdcopy --version
requires hexdump --version

out=data-partition.out
rm -f $out
cleanup_fn rm -f $out

nbdkit -U - --filter=partition data partition=1 size=1M '
  @0x1be    # MBR first partition entry
    0           # Partition status
    0 2 0       # CHS start
    0x83        # Partition type (Linux)
    0x20 0x20 0 # CHS last sector
    le32:1      # LBA first sector
    le32:0x7ff  # LBA number of sectors
  @0x1fe    # Boot signature
    0x55 0xaa

  # Data which should appear in the first partition.
  @0x200 "hello world"

  ' --run "nbdcopy \$uri $out"

hexdump -C $out

test "$( hexdump -C $out )" = '00000000  68 65 6c 6c 6f 20 77 6f  72 6c 64 00 00 00 00 00  |hello world.....|
00000010  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
000ffe00'
