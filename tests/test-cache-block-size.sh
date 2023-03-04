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

requires_filter cache
requires_nbdsh_uri

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="cache-block-size.img $sock cache-block-size.pid"
rm -f $files
cleanup_fn rm -f $files

# Create an empty base image.
$TRUNCATE -s 256K cache-block-size.img

# Run nbdkit with the caching filter.
start_nbdkit -P cache-block-size.pid -U $sock --filter=cache \
             file cache-block-size.img cache-min-block-size=128K \
             cache-on-read=true

nbdsh --connect "nbd+unix://?socket=$sock" \
      -c '
# Read half of cache-min-block-size

zero = h.pread(64 * 1024, 0)
assert zero == bytearray(64 * 1024)

buf = b"abcd" * 16 * 1024

# Write past the first read
with open("cache-block-size.img", "wb") as file:
    file.seek(64 * 1024)
    file.write(buf * 2)
    file.truncate(256 * 1024)

# Check that it got written
with open("cache-block-size.img", "rb") as file:
    file.seek(64 * 1024)
    buf2 = file.read(128 * 1024)
    assert (buf * 2) == buf2

# Now read the rest of the cache-min-block-size, it should stay empty
zero = h.pread(64 * 1024, 64 * 1024)
assert zero == bytearray(64 * 1024)

# Read past that, the pattern should be visible there
buf2 = h.pread(64 * 1024, 128 * 1024)
assert buf == buf2
'
