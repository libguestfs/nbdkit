#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018 Red Hat Inc.
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
requires nbdsh --version

sock=`mktemp -u`
files="cache-on-read.img $sock cache-on-read.pid"
rm -f $files
cleanup_fn rm -f $files

# Create an empty base image.
truncate -s 128K cache-on-read.img

# Run nbdkit with the caching filter and cache-on-read set.
start_nbdkit -P cache-on-read.pid -U $sock \
             --filter=cache \
             file cache-on-read.img \
             cache-on-read=true

nbdsh --connect "nbd+unix://?socket=$sock" \
      -c '
# Write some pattern data to the overlay and check it reads back OK.
buf = b"abcd" * 16384
h.pwrite (buf, 32768)
zero = h.pread (32768, 0)
assert zero == bytearray (32768)
buf2 = h.pread (65536, 32768)
assert buf == buf2

# XXX Suggestion to improve this test: Use the delay filter below the
# cache filter, and time reads to prove that the second read is faster
# because it is not going through the delay filter and plugin.
# XXX second h.pread here ...
'
