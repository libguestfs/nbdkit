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

# Test the data plugin with raw= parameter.

source ./functions.sh
set -e
set -x

files="data-raw.out data-raw.pid data-raw.sock"
rm -f $files

# Test that qemu-io works
if ! qemu-io --help >/dev/null; then
    echo "$0: missing or broken qemu-io"
    exit 77
fi

# Run nbdkit.
nbdkit -P data-raw.pid -U data-raw.sock \
       data raw=123 size=512

# We may have to wait a short time for the pid file to appear.
for i in `seq 1 10`; do
    if test -f data-raw.pid; then
        break
    fi
    sleep 1
done
if ! test -f data-raw.pid; then
    echo "$0: PID file was not created"
    exit 1
fi

pid="$(cat data-raw.pid)"

# Kill the nbdkit process on exit.
cleanup ()
{
    kill $pid
    rm -f $files
}
cleanup_fn cleanup

qemu-io -r -f raw 'nbd+unix://?socket=data-raw.sock' \
        -c 'r -v 0 512' | grep -E '^[[:xdigit:]]+:' > data-raw.out
if [ "$(cat data-raw.out)" != "00000000:  31 32 33 00 00 00 00 00 00 00 00 00 00 00 00 00  123.............
00000010:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000020:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000030:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000040:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000050:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000060:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000070:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000080:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000090:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000000a0:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000000b0:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000000c0:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000000d0:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000000e0:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000000f0:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000100:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000110:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000120:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000130:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000140:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000150:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000160:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000170:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000180:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000190:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000001a0:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000001b0:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000001c0:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000001d0:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000001e0:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
000001f0:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................" ]
then
    echo "$0: unexpected pattern:"
    cat data-raw.out
    exit 1
fi

# The cleanup() function is called implicitly on exit.
