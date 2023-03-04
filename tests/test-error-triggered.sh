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

requires_nbdsh_uri

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="$sock error-triggered.pid error-trigger"
rm -f $files
cleanup_fn rm -f $files

# Run nbdkit with the error filter.
start_nbdkit -P error-triggered.pid -U $sock \
             --filter=error \
             pattern 1G \
             error-rate=100% error-file=error-trigger error=ENOSPC

nbdsh --connect "nbd+unix://?socket=$sock" \
          -c '
import os

# The first operations should be fine.
h.pread(512, 0)
h.pread(512, 512)
h.pread(512, 1024)

# Then we create the error-trigger file which should cause
# subsequent operations to fail with ENOSPC.
open("error-trigger", "a").close()

try:
    h.pread(512, 0)
    # This should not happen.
    exit(1)
except nbd.Error as ex:
    # Check the errno is expected.
    assert ex.errno == "ENOSPC"

# Remove the error-trigger file, operations should all succeed again.
os.unlink("error-trigger")

h.pread(512, 0)
h.pread(512, 512)
h.pread(512, 1024)
'
