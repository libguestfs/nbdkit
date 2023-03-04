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

# Test does not run on Windows because we must mark the test file
# specially so that Windows recognizes it as sparse.  We do not have
# the tools available usually to do this (plus I also suspect that
# Wine does not emulate this properly).
if is_windows; then
    echo "$0: this test needs to be revised to work on Windows"
    exit 77
fi

requires_filter cow
requires_plugin file

requires test -r /dev/urandom

requires $CUT --version
requires dd --version
requires_nbdinfo
requires nbdsh --version
requires $STAT --version
requires tr --version
requires $TRUNCATE --version

if ! nbdinfo --help | grep -- --map ; then
    echo "$0: nbdinfo --map option required to run this test"
    exit 77
fi

base=cow-extents1-base.img
pid=cow-extents1.pid
out=cow-extents1.out
sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="$base $pid $out $sock"
rm -f $files
cleanup_fn rm -f $files

# Create a base file which is half allocated, half sparse.
dd if=/dev/urandom of=$base count=128 bs=1K
$TRUNCATE -s 4M $base
lastmod="$($STAT -c "%y" $base)"

# Run nbdkit with a COW overlay.
start_nbdkit -P $pid -U $sock --filter=cow file $base
uri="nbd+unix:///?socket=$sock"

# The map should reflect the base image.
nbdinfo --map "$uri" > $out
cat $out
if [ "$(tr -s ' ' < $out | $CUT -d' ' -f 1-4)" != " 0 131072 0
 131072 4063232 3" ]; then
    echo "$0: unexpected initial file map"
    exit 1
fi

# Punch some holes.
nbdsh -u "$uri" \
      -c 'bs = 65536' \
      -c 'h.trim(bs, bs)' \
      -c 'h.trim(bs+2, 4*bs-1)' \
      -c 'h.pwrite(b"1"*bs, 16*bs)' \
      -c 'h.trim(2*bs, 32*bs)' \
      -c 'h.pwrite(b"2"*(2*bs), 48*bs)'

# The extents map should be fully allocated.
nbdinfo --map "$uri" > $out
cat $out
if [ "$(tr -s ' ' < $out | $CUT -d' ' -f 1-4)" != " 0 65536 0
 65536 131072 3
 196608 65536 0
 262144 65536 3
 327680 65536 0
 393216 655360 3
 1048576 65536 0
 1114112 2031616 3
 3145728 131072 0
 3276800 917504 3" ]; then
    echo "$0: unexpected trimmed file map"
    exit 1
fi

# The original file must not be modified.
currmod="$($STAT -c "%y" $base)"
if [ "$lastmod" != "$currmod" ]; then
    echo "$0: FAILED last modified time of base file changed"
    exit 1
fi
