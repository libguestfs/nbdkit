#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018-2020 Red Hat Inc.
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

requires_unix_domain_sockets
requires qemu-img --version

files="readahead-copy1.img readahead-copy2.img"
rm -f $files
cleanup_fn rm -f $files

# Run nbdkit with and without the readahead filter and a very sparse
# disk with bytes scattered around.  Compare the two outputs which
# should be identical.
#
# Note we must set the size to a multiple of 512 bytes else qemu-img
# cannot cope.
data="            1
         @0x10001 2
        @0x100020 3
       @0x1000301 4
      @0x10004020 5
      @0x20050301 6
      @0x40604020 7
      @0x87050300 8
     @0x100000000 9 "
size=$((2**32 + 512))

nbdkit -v -U - --filter=readahead data "$data" size=$size \
       --run 'qemu-img convert $nbd readahead-copy1.img'
nbdkit -v -U -                    data "$data" size=$size \
       --run 'qemu-img convert $nbd readahead-copy2.img'

# Check the output.
cmp readahead-copy{1,2}.img

