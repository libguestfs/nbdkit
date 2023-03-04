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
files="cache-unaligned.img $sock cache-unaligned.pid"
rm -f $files
cleanup_fn rm -f $files

# Create an empty base image which is not a multiple of 4K.
$TRUNCATE -s 130000 cache-unaligned.img

# Run nbdkit with the caching filter.
start_nbdkit -P cache-unaligned.pid -U $sock --filter=cache \
             file cache-unaligned.img

nbdsh --connect "nbd+unix://?socket=$sock" \
      -c '
# Write some pattern data to the overlay and check it reads back OK.
buf = b"abcdefghijklm" * 10000
h.pwrite(buf, 0)
buf2 = h.pread(130000, 0)
assert buf == buf2

# Flushing should write through to the underlying file.
h.flush()

with open("cache-unaligned.img", "rb") as file:
    buf2 = file.read(130000)
    assert buf == buf2
'
