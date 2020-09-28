#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2014 Red Hat Inc.
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

# Test nbdkit -f option.

source ./functions.sh
set -e
set -x

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="foreground.pid $sock"
rm -f $files
cleanup_fn rm -f $files

nbdkit -f -P foreground.pid -U $sock example1 &
bg_pid=$!

# We may have to wait a short time for the pid file to appear.
for i in {1..60}; do
    if test -f foreground.pid; then
        break
    fi
    sleep 1
done
if ! test -f foreground.pid; then
    echo "$0: PID file was not created"
    exit 1
fi

pid="$(cat foreground.pid)"
cleanup_fn kill $pid

# Check the backgrounded PID (from $!) is the same as the PID reported
# by nbdkit.  Note this is not true for Windows because the wrapper
# has to use spawn since exec isn't really a thing.
if ! is_windows; then
    test "$bg_pid" -eq "$pid"
fi

# Check the socket was created (and is a socket).
test -S $sock

# Kill the process.
if ! is_windows; then
    kill $pid
else
    wine taskkill /f /pid $pid
fi

# Check the process exits (eventually).
for i in {1..10}; do
    if ! kill -s 0 $bg_pid; then
        break;
    fi
    sleep 1
done
if kill -s 0 $bg_pid; then
    echo "$0: process did not exit after sending a signal"
    exit 1
fi
