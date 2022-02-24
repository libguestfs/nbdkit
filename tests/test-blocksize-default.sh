#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2019-2022 Red Hat Inc.
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

source ./functions.sh
set -e
set -x

requires_plugin eval
requires nbdsh -c 'print(h.get_block_size)'

# Create an nbdkit eval plugin which presents per-export block size
# constraints based on the export name.
# Check that the blocksize filter advertises full access, that size is
# truncated to minblock constraints, and underlying plugin access uses
# read-modify-write at appropriate boundaries.
export script='
import os

sock = os.environ["unixsocket"]

ha = h;
ha.set_export_name("a")
ha.connect_unix(sock)
hb = nbd.NBD()
hb.set_export_name("b")
hb.connect_unix(sock)

assert ha.get_size() == 2 * 1024 * 1024 - 1 * 1024
assert ha.get_block_size(nbd.SIZE_MINIMUM) == 1
assert ha.get_block_size(nbd.SIZE_PREFERRED) == 128 * 1024
assert ha.get_block_size(nbd.SIZE_MAXIMUM) == 4 * 1024 * 1024 * 1024 - 1
assert ha.pread(1024 * 1024 + 1, 1) == b"\0" * (1024 * 1024 + 1)

assert hb.get_size() == 2 * 1024 * 1024 - 2 * 1024
assert hb.get_block_size(nbd.SIZE_MINIMUM) == 1
assert hb.get_block_size(nbd.SIZE_PREFERRED) == 64 * 1024
assert hb.get_block_size(nbd.SIZE_MAXIMUM) == 4 * 1024 * 1024 * 1024 - 1
assert hb.pread(1024 * 1024 + 1, 1) == b"\0" * (1024 * 1024 + 1)

ha.shutdown()
hb.shutdown()
'
nbdkit -v -U - --filter=blocksize eval \
    open='echo $3' \
    get_size="echo $((2 * 1024 * 1024 - 1))" \
    block_size='case $2 in
      a) echo 1K 128K 512K ;;
      b) echo 2K 64K 800K ;;
      *) echo unknown export name >&2; exit 1 ;;
    esac' \
    pread='case $2.$3.$4 in
      a.$((1*1024)).0 | a.$((512*1024)).$((1*1024)) | \
      a.$((511*1024)).$((513*1024)) | a.$((1*1024)).$((1024*1024)) | \
      b.$((2*1024)).0 | b.$((800*1024)).$((2*1024)) | \
      b.$((222*1024)).$((802*1024)) | b.$((2*1024)).$((1024*1024)) )
        dd if=/dev/zero count=$3 iflag=count_bytes ;;
      *) echo EIO >&2; exit 1 ;;
    esac' \
    --run 'export unixsocket; nbdsh -c "$script"'
