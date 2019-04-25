#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2019 Red Hat Inc.
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

# Regression test when next_ops->get_size changes between connections.
# For now, NBD does not support dynamic resize; but the file plugin
# reads size from the file system for each new connection, at which
# point the client remembers that size for the life of the connection.
# We are testing that connection A can still see the tail of a file,
# even when connection B is opened while the file was temporarily
# shorter (if the actions of connection B affect the size visible
# through connection A, we didn't isolate per-connection state).

source ./functions.sh
set -e
set -x

requires qemu-io --version

sock=`mktemp -u`
files="truncate4.out truncate4.pid $sock truncate4.data"
rm -f $files
cleanup_fn rm -f $files

# Initial file contents: 1k of pattern 1
truncate -s 1024 truncate4.data
qemu-io -c 'w -P 1 0 1024' -f raw truncate4.data

# Run nbdkit with file plugin and truncate filter in front.
start_nbdkit -P truncate4.pid -U $sock \
       --filter=truncate \
       file truncate4.data \
       round-up=1024

fail=0
exec 4>&1 # Save original stdout
{
    exec 5>&1 >&4 # Save connection A, set stdout back to original
    echo 'Reading from connection A, try 1'
    echo 'r -P 1 0 1024' >&5
    sleep 1
    echo 'Resizing down'
    truncate -s 512 truncate4.data
    echo 'Reading from connection B'
    echo 'r -P 1 0 512' | qemu-io -f raw nbd:unix:$sock >> truncate4.out
    echo 'Restoring size'
    truncate -s 1024 truncate4.data
    qemu-io -c 'w -P 2 0 1024' -f raw truncate4.data
    echo 'Reading from connection A, try 2'
    echo 'r -P 2 512 512' >&5
    echo 'quit' >&5
} | qemu-io -f raw nbd:unix:$sock >> truncate4.out || fail=1
exec 4>&-

cat truncate4.out
grep 'Pattern verification failed' truncate4.out && fail=1
exit $fail
