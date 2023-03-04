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

# Test nbdkit starts up, forks in the background, writes a PID file,
# and can be killed.

source ./functions.sh
set -e
set -x

# Cannot use kill pidfile below to test if the process is running on
# Windows.
if is_windows; then
    echo "$0: this test needs to be revised to work on Windows"
    exit 77
fi

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
rm -f start.pid $sock
cleanup_fn rm -f start.pid $sock

start_nbdkit -P start.pid -U $sock example1
pid="$(cat start.pid)"

# Check the process exists.
kill -s 0 $pid

# Check the socket was created (and is a socket).
test -S $sock

# Kill the process.
kill $pid

# Check the process exits (eventually).
for i in {1..10}; do
    if ! kill -s 0 $pid; then
        break;
    fi
    sleep 1
done
if kill -s 0 $pid; then
    echo "$0: process did not exit after sending a signal"
    exit 1
fi
