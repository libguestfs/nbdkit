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

# Test the memory plugin with the malloc allocator and mlock.  Usually
# mlock limits are very low, so this test only tries to allocate a
# very tiny disk, in the hope that this way we at least end up with
# some test coverage of the feature.

source ./functions.sh
set -e

requires_nbdsh_uri

if ! nbdkit memory --dump-plugin | grep -sq mlock=yes; then
    echo "$0: mlock not enabled in this build of nbdkit"
    exit 77
fi

# ulimit -l is measured in kilobytes and so for this test must be at
# least 10 (kilobytes) and we actually check it's a bit larger to
# allow room for error.  On Linux the default is usually 64.
if test "$(ulimit -l)" != "unlimited" && test "$(ulimit -l)" -le 16; then
    echo "$0: limit for mlock memory (ulimit -l) too low for test"
    exit 77
fi

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="memory-allocator-malloc-mlock.pid $sock"
rm -f $files
cleanup_fn rm -f $files

# Run nbdkit with memory plugin.
start_nbdkit -P memory-allocator-malloc-mlock.pid -U $sock \
             memory 10240 allocator=malloc,mlock=true

nbdsh --connect "nbd+unix://?socket=$sock" \
      -c '
# Write some stuff.
buf1 = b"1" * 512
h.pwrite(buf1, 0)
buf2 = b"2" * 512
h.pwrite(buf2, 1024)
buf3 = b"3" * 512
h.pwrite(buf3, 1536)
buf4 = b"4" * 512
h.pwrite(buf4, 4096)
buf5 = b"5" * 1024
h.pwrite(buf5, 10240-len(buf5))

# Read it back.
buf11 = h.pread(len(buf1), 0)
assert buf1 == buf11
buf22 = h.pread(len(buf2), 1024)
assert buf2 == buf22
buf33 = h.pread(len(buf3), 1536)
assert buf3 == buf33
buf44 = h.pread(len(buf4), 4096)
assert buf4 == buf44
buf55 = h.pread(len(buf5), 10240-len(buf5))
assert buf5 == buf55
'
