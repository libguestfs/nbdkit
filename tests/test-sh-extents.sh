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

source ./functions.sh
set -e
set -x

requires_unix_domain_sockets
requires jq --version
requires qemu-img --version
requires qemu-img map --help

out="test-sh-extents.out"
expected="test-sh-extents.expected"
files="$out $expected"
rm -f $files
cleanup_fn rm -f $files

do_test ()
{
    # We use jq to normalize the output and convert it to plain text.
    nbdkit -v -U - sh - --run 'qemu-img map -f raw --output=json $nbd' |
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
do_test <<'EOF'
case "$1" in
     thread_model) echo parallel ;;
     get_size) echo 64K ;;
     can_extents) exit 0 ;;
     extents)
         echo "0   64K   hole,zero"
         ;;
     *) exit 2 ;;
esac
EOF

# Completely allocated disk.
# type field missing means allocated data.
cat > $expected <<'EOF'
{"start":0,"length":65536,"data":true,"zero":false}
EOF
do_test <<'EOF'
case "$1" in
     thread_model) echo parallel ;;
     get_size) echo 64K ;;
     can_extents) exit 0 ;;
     extents)
         echo "0 64K"
         ;;
     *) exit 2 ;;
esac
EOF

# Completely allocated disk.
# type=0
cat > $expected <<'EOF'
{"start":0,"length":65536,"data":true,"zero":false}
EOF
do_test <<'EOF'
case "$1" in
     thread_model) echo parallel ;;
     get_size) echo 64K ;;
     can_extents) exit 0 ;;
     extents)
         echo "0 64K 0"
         ;;
     *) exit 2 ;;
esac
EOF

# Allocated data at the start of a 1M disk.
cat > $expected <<'EOF'
{"start":0,"length":32768,"data":true,"zero":false}
{"start":32768,"length":1015808,"data":false,"zero":true}
EOF
do_test <<'EOF'
case "$1" in
     thread_model) echo parallel ;;
     get_size) echo 1M ;;
     can_extents) exit 0 ;;
     extents)
         echo "0   32K"
         echo "32K 1015808 hole,zero"
         ;;
     *) exit 2 ;;
esac
EOF

# Zeroes at the start of a 1M disk.
cat > $expected <<'EOF'
{"start":0,"length":32768,"data":true,"zero":true}
{"start":32768,"length":32768,"data":false,"zero":true}
{"start":65536,"length":983040,"data":true,"zero":false}
EOF
do_test <<'EOF'
case "$1" in
     thread_model) echo parallel ;;
     get_size) echo 1M ;;
     can_extents) exit 0 ;;
     extents)
         echo "0   32K    zero"
         echo "32K 32K    hole,zero"
         echo "64K 983040 "
         ;;
     *) exit 2 ;;
esac
EOF

# If can_extents is not defined, extents should never be called.
cat > $expected <<'EOF'
{"start":0,"length":65536,"data":true,"zero":false}
EOF
do_test <<'EOF'
case "$1" in
     thread_model) echo parallel ;;
     get_size) echo 64K ;;
     extents) exit 1 ;;
     *) exit 2 ;;
esac
EOF
