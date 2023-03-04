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

# Test the data plugin with > 4G (64 bit) nesting.
#
# This was broken before 1.23.5 because 64 bit counts were being
# truncated to 32 bit in the allocator layer.

source ./functions.sh
set -e
set -x

requires_nbdsh_uri

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="data-64b.pid $sock"
rm -f $files
cleanup_fn rm -f $files

# Run nbdkit.
start_nbdkit -P data-64b.pid -U $sock \
       data '
# Construct a named subexpression which is larger than 32 bits
# in size and has markers at the beginning and end.  This is 8 GB
# (sparse).
( 0x55 0xAA @0x200000000 @-2 0x55 0xAA ) -> \1

# Construct a sparse disk containing several copies using the "*"
# operator.  The total disk image will be 8*4 = 32 GB (sparse).
\1 * 4
'

nbdsh --connect "nbd+unix://?socket=$sock" \
      -c '
print ("%d" % h.get_size())
assert h.get_size() == 0x800000000

# Read the markers.
for i in range(4):
    offset = i * 0x200000000
    buf = h.pread(2, offset)
    assert buf == b"\x55\xAA"
    buf = h.pread(2, offset + 0x200000000 - 2)
    assert buf == b"\x55\xAA"
'
