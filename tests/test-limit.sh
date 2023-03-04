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
set -e
set -x

requires nbdsh --version

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="limit.pid $sock"
cleanup_fn rm -f $files

# Start nbdkit with the limit filter and a limit of 2 clients.
start_nbdkit -P limit.pid -U $sock --filter=limit memory size=1M limit=2

export sock

nbdsh -c - <<'EOF'
import os
import sys
import time

sock = os.environ["sock"]

# It should be possible to connect two clients.
# Note that nbdsh creates the ‘h’ handle implicitly.
h.connect_unix(sock)
h2 = nbd.NBD()
h2.connect_unix(sock)

# A third connection is expected to fail.
try:
    h3 = nbd.NBD()
    h3.connect_unix(sock)
    # This should not happen.
    sys.exit(1)
except nbd.Error:
    pass

# Close one of the existing connections.
del h2

# There's a possible race between closing the client socket
# and nbdkit noticing and closing the connection.
time.sleep(5)

# Now a new connection should be possible.
h4 = nbd.NBD()
h4.connect_unix(sock)

EOF
