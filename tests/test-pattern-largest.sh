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

# Test the pattern plugin with the largest possible size supported by
# nbdkit.  By using nbdsh (libnbd) as the client we are able to read
# the final 511 byte "sector" which eludes qemu.

source ./functions.sh
set -e

requires_nbdsh_uri

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="pattern-largest.pid $sock"
rm -f $files
cleanup_fn rm -f $files

# Run nbdkit with pattern plugin.
# size = 2^63-1
start_nbdkit -P pattern-largest.pid -U $sock \
       pattern 9223372036854775807

nbdsh --connect "nbd+unix://?socket=$sock" \
      -c '
# Construct what we expect to see in the last 511 bytes.
expected = b""
for i in range(0, 512-8, 8):
    expected = expected + \
               bytes([0x7f, 0xff, 0xff, 0xff, \
                      0xff, 0xff, 0xfe + (i >> 8), i & 255])
# final byte is truncated so we have to deal with last 7
# bytes specially ...
expected = expected + b"\x7f\xff\xff\xff\xff\xff\xff"

# Read and compare.
buf = h.pread(511, 9223372036854775296)
print("buf = %r\nexpected = %r\n" % (buf, expected))
assert buf == expected
'
