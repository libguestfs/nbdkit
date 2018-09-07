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

set -e

files="log.img log.log log.sock log.pid"
rm -f $files

# Test that qemu-io works
truncate -s 10M log.img
if ! qemu-io -f raw -c 'w 1M 2M' log.img; then
    echo "$0: missing or broken qemu-io"
    exit 77
fi

# Run nbdkit with logging enabled to file.
nbdkit -P log.pid -U log.sock --filter=log file log.img logfile=log.log

# We may have to wait a short time for the pid file to appear.
for i in `seq 1 10`; do
    if test -f log.pid; then
        break
    fi
    sleep 1
done
if ! test -f log.pid; then
    echo "$0: PID file was not created"
    exit 1
fi

pid="$(cat log.pid)"

# Kill the nbdkit process on exit.
cleanup ()
{
    status=$?
    trap '' INT QUIT TERM EXIT ERR
    echo $0: cleanup: exit code $status

    kill $pid
    # For easier debugging, dump the final log file before removing it.
    echo "Log file contents:"
    cat log.log
    rm -f $files

    exit $status
}
trap cleanup INT QUIT TERM EXIT ERR

# Write, then read some data in the file.
qemu-io -f raw -c 'w -P 11 1M 2M' 'nbd+unix://?socket=log.sock'
qemu-io -r -f raw -c 'r -P 11 2M 1M' 'nbd+unix://?socket=log.sock'

# The log should show a write on connection 1, and read on connection 2.
grep 'connection=1 Write id=1 offset=0x100000 count=0x200000 ' log.log
grep 'connection=2 Read id=1 offset=0x200000 count=0x100000 ' log.log

# The cleanup() function is called implicitly on exit.
