#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018 Red Hat Inc.
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

# Additional test of the offset filter using the pattern plugin.

source ./functions.sh
set -e
set -x

requires_unix_domain_sockets
requires qemu-io --version

sock=`mktemp -u`
files="offset2.out offset2.pid $sock"
rm -f $files
cleanup_fn rm -f $files

# Run nbdkit with pattern plugin and offset filter in front.
# 8070450532247927809 = 7E - 1023
start_nbdkit -P offset2.pid -U $sock \
       --filter=offset \
       pattern 7E \
       offset=8070450532247927809 range=512

qemu-io -r -f raw "nbd+unix://?socket=$sock" \
        -c 'r -v 0 512' | grep -E '^[[:xdigit:]]+:' > offset2.out
if [ "$(cat offset2.out)" != "00000000:  ff ff ff ff ff fc 00 6f ff ff ff ff ff fc 08 6f  .......o.......o
00000010:  ff ff ff ff ff fc 10 6f ff ff ff ff ff fc 18 6f  .......o.......o
00000020:  ff ff ff ff ff fc 20 6f ff ff ff ff ff fc 28 6f  .......o.......o
00000030:  ff ff ff ff ff fc 30 6f ff ff ff ff ff fc 38 6f  ......0o......8o
00000040:  ff ff ff ff ff fc 40 6f ff ff ff ff ff fc 48 6f  .......o......Ho
00000050:  ff ff ff ff ff fc 50 6f ff ff ff ff ff fc 58 6f  ......Po......Xo
00000060:  ff ff ff ff ff fc 60 6f ff ff ff ff ff fc 68 6f  .......o......ho
00000070:  ff ff ff ff ff fc 70 6f ff ff ff ff ff fc 78 6f  ......po......xo
00000080:  ff ff ff ff ff fc 80 6f ff ff ff ff ff fc 88 6f  .......o.......o
00000090:  ff ff ff ff ff fc 90 6f ff ff ff ff ff fc 98 6f  .......o.......o
000000a0:  ff ff ff ff ff fc a0 6f ff ff ff ff ff fc a8 6f  .......o.......o
000000b0:  ff ff ff ff ff fc b0 6f ff ff ff ff ff fc b8 6f  .......o.......o
000000c0:  ff ff ff ff ff fc c0 6f ff ff ff ff ff fc c8 6f  .......o.......o
000000d0:  ff ff ff ff ff fc d0 6f ff ff ff ff ff fc d8 6f  .......o.......o
000000e0:  ff ff ff ff ff fc e0 6f ff ff ff ff ff fc e8 6f  .......o.......o
000000f0:  ff ff ff ff ff fc f0 6f ff ff ff ff ff fc f8 6f  .......o.......o
00000100:  ff ff ff ff ff fd 00 6f ff ff ff ff ff fd 08 6f  .......o.......o
00000110:  ff ff ff ff ff fd 10 6f ff ff ff ff ff fd 18 6f  .......o.......o
00000120:  ff ff ff ff ff fd 20 6f ff ff ff ff ff fd 28 6f  .......o.......o
00000130:  ff ff ff ff ff fd 30 6f ff ff ff ff ff fd 38 6f  ......0o......8o
00000140:  ff ff ff ff ff fd 40 6f ff ff ff ff ff fd 48 6f  .......o......Ho
00000150:  ff ff ff ff ff fd 50 6f ff ff ff ff ff fd 58 6f  ......Po......Xo
00000160:  ff ff ff ff ff fd 60 6f ff ff ff ff ff fd 68 6f  .......o......ho
00000170:  ff ff ff ff ff fd 70 6f ff ff ff ff ff fd 78 6f  ......po......xo
00000180:  ff ff ff ff ff fd 80 6f ff ff ff ff ff fd 88 6f  .......o.......o
00000190:  ff ff ff ff ff fd 90 6f ff ff ff ff ff fd 98 6f  .......o.......o
000001a0:  ff ff ff ff ff fd a0 6f ff ff ff ff ff fd a8 6f  .......o.......o
000001b0:  ff ff ff ff ff fd b0 6f ff ff ff ff ff fd b8 6f  .......o.......o
000001c0:  ff ff ff ff ff fd c0 6f ff ff ff ff ff fd c8 6f  .......o.......o
000001d0:  ff ff ff ff ff fd d0 6f ff ff ff ff ff fd d8 6f  .......o.......o
000001e0:  ff ff ff ff ff fd e0 6f ff ff ff ff ff fd e8 6f  .......o.......o
000001f0:  ff ff ff ff ff fd f0 6f ff ff ff ff ff fd f8 6f  .......o.......o" ]
then
    echo "$0: unexpected pattern:"
    cat offset2.out
    exit 1
fi
