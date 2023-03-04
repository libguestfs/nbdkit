#!/usr/bin/env bash
# nbdkit
# Copyright Red Hat
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
set -x

# Cannot use kill pidfile below to test if the process is running on
# Windows.
if is_windows; then
    echo "$0: this test needs to be revised to work on Windows"
    exit 77
fi

requires_nbdinfo
requires qemu-io --version

plugin=.libs/test-shutdown-plugin.$SOEXT
requires test -f $plugin

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="shutdown.pid $sock"
cleanup_fn rm -f $files

# Start nbdkit with the shutdown plugin.
start_nbdkit -P shutdown.pid -U $sock $plugin

pid=`cat shutdown.pid`

# Reads are fine, nbdkit should still be running afterwards.
for i in 1 2 3; do
    nbdinfo "nbd+unix:///?socket=$sock"
    sleep 2
    kill -s 0 $pid
done

# Writing should cause nbdkit to exit, although it may take time.
qemu-io -f raw -c 'w -P 0x55 0 1024' "nbd:unix:$sock"

# Wait for nbdkit process to exit
for i in {1..60}; do
    if ! kill -s 0 $pid; then
        break
    fi
    sleep 1
done
if kill -s 0 $pid; then
    echo "$0: nbdkit $plugin did not exit as expected"
    exit 1
fi
