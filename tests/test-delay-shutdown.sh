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

# Cannot use kill pidfile below on Windows, but must use taskkill instead.
if is_windows; then
    echo "$0: this test needs to be revised to work on Windows"
    exit 77
fi

requires qemu-io --version
requires timeout 60s true

pidfile=delay-shutdown.pid
sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="$pidfile $sock"
cleanup_fn rm -f $files
fail=0

# Create a server that delays reads and forces only one connection at a time.
# This tests that the delay filter's use of nbdkit_nanosleep is able to
# react to both connection death and server shutdown without finishing
# the entire delay duration.
start_nbdkit -P $pidfile -U $sock --filter=noparallel --filter=delay \
    null 1M serialize=connections rdelay=60

# Early client death should not stall connection of second client.
trap '' ERR

# Find out the exit status for timed out processes
timeout 1s sleep 5 >/dev/null
timeout_status=$?
if test $timeout_status = 0; then
    echo "Failed to get timeout exit status"
    exit 1
fi

timeout 1s qemu-io -f raw "nbd+unix:///?socket=$sock" -c 'r 0 512' </dev/null
test $? = $timeout_status || {
    echo "Unexpected status; qemu-io should have been killed for timing out"
    fail=1
}
timeout 5s qemu-io -f raw "nbd+unix:///?socket=$sock" -c 'quit' </dev/null
test $? = 0 || {
    echo "Unexpected status; nbdkit was not responsive to allow second qemu-io"
    fail=1
}

# The server's response to shutdown signals should not stall on delays
qemu-io -f raw "nbd+unix:///?socket=$sock" -c 'r 0 512' </dev/null &
pid=$!
sleep 1
kill -s INT "$(cat "$pidfile")"
for (( i=0; i < 5; i++ )); do
    kill -s 0 "$(cat "$pidfile")" || break
    sleep 1
done
if [ $i = 5 ]; then
    echo "Unexpected status; nbdkit didn't react fast enough to signal"
    fail=1
fi
wait $pid

exit $fail
