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

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
pidfile=exitwhen-file-created-reject-new.pid
eventfile=exitwhen-file-created-reject-new.event
files="$pidfile $sock $eventfile"
cleanup_fn rm -f $files

# Start nbdkit with the exitwhen filter.
start_nbdkit -P $pidfile -U $sock \
             --filter=exitwhen memory size=1M \
             exit-when-file-created=$eventfile

# Connect and test.
export eventfile sock
nbdsh -c - <<'EOF'
import os
from pathlib import Path

eventfile = os.environ['eventfile']
sock = os.environ['sock']

# Open a single connection.
h.connect_unix(sock)

# Check the connection is functional.
buf = h.pread(512, 0)

# Create the event.
Path(eventfile).touch()

# A new connection should be rejected.
h2 = nbd.NBD()
try:
    h2.connect_unix(sock)
    exit(1)
except nbd.Error:
    pass
EOF

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
