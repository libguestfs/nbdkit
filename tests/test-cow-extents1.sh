#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018-2021 Red Hat Inc.
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

requires_filter cow
requires_plugin file

requires test -r /dev/urandom

requires dd --version
requires nbdinfo --version
requires nbdsh --version
requires tr --version
requires truncate --version

base=cow-extents1-base.img
pid=cow-extents1.pid
out=cow-extents1.out
sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="$base $pid $out $sock"
rm -f $files
cleanup_fn rm -f $files

# Create a base file which is half allocated, half sparse.
dd if=/dev/urandom of=$base count=128 bs=1K
truncate -s 256K $base
lastmod="$(stat -c "%y" $base)"

# Run nbdkit with a COW overlay.
start_nbdkit -P $pid -U $sock --filter=cow file $base
uri="nbd+unix:///?socket=$sock"

# The map should reflect the base image.
nbdinfo --map "$uri" > $out
cat $out
if [ "$(tr -s ' ' < $out)" != " 0 131072 0 allocated
 131072 131072 3 hole,zero" ]; then
    echo "$0: unexpected initial file map"
    exit 1
fi

# Punch some holes.
nbdsh -u "$uri" \
      -c 'h.trim(4096, 4096)' \
      -c 'h.trim(4098, 16383)' \
      -c 'h.pwrite(b"1"*4096, 65536)' \
      -c 'h.trim(8192, 131072)' \
      -c 'h.pwrite(b"2"*8192, 196608)'

# The extents map should be fully allocated.
nbdinfo --map "$uri" > $out
cat $out
if [ "$(tr -s ' ' < $out)" != " 0 4096 0 allocated
 4096 4096 3 hole,zero
 8192 8192 0 allocated
 16384 4096 3 hole,zero
 20480 110592 0 allocated
 131072 65536 3 hole,zero
 196608 8192 0 allocated
 204800 57344 3 hole,zero" ]; then
    echo "$0: unexpected trimmed file map"
    exit 1
fi

# The original file must not be modified.
currmod="$(stat -c "%y" $base)"
if [ "$lastmod" != "$currmod" ]; then
    echo "$0: FAILED last modified time of base file changed"
    exit 1
fi
