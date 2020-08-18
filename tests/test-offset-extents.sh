#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018-2020 Red Hat Inc.
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

# Unfortunately the output of this test depends on the PAGE_SIZE
# defined in common/allocators/sparse.c and would change (breaking the
# test) if we ever changed that definition.

source ./functions.sh
set -e
set -x

requires_unix_domain_sockets
requires jq --version
requires qemu-img --version
requires qemu-img map --help

out="test-offset-extents.out"
expected="test-offset-extents.expected"
files="$out $expected"
rm -f $files
cleanup_fn rm -f $files

do_test ()
{
    # We use jq to normalize the output and convert it to plain text.
    nbdkit -U - \
           --filter=offset \
           data "$1" size="$2" \
           offset=1024 range=65536 \
           --run 'qemu-img map -f raw --output=json $nbd' |
        jq -c '.[] | {start:.start, length:.length, data:.data, zero:.zero}' \
           > $out
    if ! cmp $out $expected; then
        echo "$0: output did not match expected data"
        echo "expected:"
        cat $expected
        echo "output:"
        cat $out
        exit 1
    fi
}

# Completely sparse disk.
cat > $expected <<'EOF'
{"start":0,"length":65536,"data":false,"zero":true}
EOF
do_test "" 1M

# Completely allocated disk.
#
# We have to write a non-zero byte @32767 so that the sparse array
# doesn't detect the range from 1024-32767 as being all zeroes.
cat > $expected <<'EOF'
{"start":0,"length":65536,"data":true,"zero":false}
EOF
do_test "1 @32767 1 @32768 1 @65536 1 @98304 1" 128K

# Allocated data at the start of a 1M disk.
cat > $expected <<'EOF'
{"start":0,"length":31744,"data":true,"zero":false}
{"start":31744,"length":33792,"data":false,"zero":true}
EOF
do_test "1 @32767 1" 1M

# Allocated zeroes at the start of the disk.
cat > $expected <<'EOF'
{"start":0,"length":31744,"data":true,"zero":true}
{"start":31744,"length":33792,"data":false,"zero":true}
EOF
do_test "0" 1M
