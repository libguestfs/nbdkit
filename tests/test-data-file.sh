#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018 Red Hat Inc.
# All rights reserved.
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

# Test the data plugin with an inline file.

source ./functions.sh
set -e
set -x

requires qemu-io --version

files="data-file.out data-file.pid data-file.sock data-hello.txt"
rm -f $files
cleanup_fn rm -f $files

rm -f data-hello.txt
for i in {0..1000}; do
    echo -n "hello " >> data-hello.txt
done

# Run nbdkit.
start_nbdkit -P data-file.pid -U data-file.sock \
       --filter=partition \
       data partition=1 \
       size=1M \
       data="
   @0x1b8 178 190 207 221 0 0 0 0 2 0 131 32 32 0 1 0 0 0 255 7
   @0x1fe 85 170
   @0x200 <data-hello.txt
   "

# Since we're reading the empty first partition, any read returns zeroes.
qemu-io -r -f raw 'nbd+unix://?socket=data-file.sock' \
        -c 'r -v 0 900' | grep -E '^[[:xdigit:]]+:' > data-file.out
if [ "$(cat data-file.out)" != "00000000:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
00000010:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
00000020:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
00000030:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
00000040:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
00000050:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
00000060:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
00000070:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
00000080:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
00000090:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
000000a0:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
000000b0:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
000000c0:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
000000d0:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
000000e0:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
000000f0:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
00000100:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
00000110:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
00000120:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
00000130:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
00000140:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
00000150:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
00000160:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
00000170:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
00000180:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
00000190:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
000001a0:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
000001b0:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
000001c0:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
000001d0:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
000001e0:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
000001f0:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
00000200:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
00000210:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
00000220:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
00000230:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
00000240:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
00000250:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
00000260:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
00000270:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
00000280:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
00000290:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
000002a0:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
000002b0:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
000002c0:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
000002d0:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
000002e0:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
000002f0:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
00000300:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
00000310:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
00000320:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
00000330:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
00000340:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
00000350:  6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20  llo.hello.hello.
00000360:  68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65 6c 6c  hello.hello.hell
00000370:  6f 20 68 65 6c 6c 6f 20 68 65 6c 6c 6f 20 68 65  o.hello.hello.he
00000380:  6c 6c 6f 20  llo." ]
then
    echo "$0: unexpected pattern:"
    cat data-file.out
    exit 1
fi
