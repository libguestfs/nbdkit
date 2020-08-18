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

out="test-truncate-extents.out"
expected="test-truncate-extents.expected"
files="$out $expected"
rm -f $files
cleanup_fn rm -f $files

do_test ()
{
    # We use jq to normalize the output and convert it to plain text.
    nbdkit -U - \
           --filter=truncate \
           data "$1" size="$2" \
           truncate="$3" \
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
do_test "" 1M 65536

# Completely allocated disk.
cat > $expected <<'EOF'
{"start":0,"length":32768,"data":true,"zero":false}
EOF
do_test "1 @32768 1 @65536 1 @98304 1" 128K 32768

#----------------------------------------------------------------------
# The above are the easy cases.  Now let's truncate to a larger
# size which should create a hole at the end.

# Completely sparse disk.
cat > $expected <<'EOF'
{"start":0,"length":1048576,"data":false,"zero":true}
EOF
do_test "" 65536 1M

# Completely allocated disk.
cat > $expected <<'EOF'
{"start":0,"length":512,"data":true,"zero":false}
{"start":512,"length":1048064,"data":false,"zero":true}
EOF
do_test "1" 512 1M

# Zero-length plugin.  Unlike nbdkit-zero-plugin, the data plugin
# advertises extents and so will behave differently.
cat > $expected <<'EOF'
{"start":0,"length":0,"data":false,"zero":false}
EOF
do_test "" 0 0
