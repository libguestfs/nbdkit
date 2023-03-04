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

requires_filter exitwhen
requires nbdsh --version

if is_windows; then
    echo "$0: exit-when-process-exits parameter not supported by Windows port"
    exit 77
fi

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
pidfile=exitwhen-process-exits.pid
files="$pidfile $sock"
cleanup_fn rm -f $files

# Start the unrelated process.
sleep $(( 60 * 60 )) &
sleeppid=$!

# Start nbdkit with the exitwhen filter.
start_nbdkit -P $pidfile -U $sock \
             --filter=exitwhen memory size=1M \
             exit-when-process-exits=$sleeppid \
             exit-when-poll=1

# Killing the sleep process should cause nbdkit to exit after a few
# seconds.
kill $sleeppid

# Now nbdkit should exit automatically.  Wait for it to do so.
pid=`cat $pidfile`
for i in {1..60}; do
    if ! kill -s 0 $pid; then
        break
    fi
    sleep 1
done
if kill -s 0 $pid; then
    echo "$0: nbdkit did not exit after last client connection"
    exit 1
fi
