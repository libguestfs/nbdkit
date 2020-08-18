#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2016-2020 Red Hat Inc.
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

# Test the extentlist filter.

source ./functions.sh
set -e
set -x

requires_unix_domain_sockets
requires jq --version
requires qemu-img --version
requires qemu-img map --help

out=test-extentlist.out
input=test-extentlist.in
expected=test-extentlist.expected
files="$out $input $expected"
rm -f $files
cleanup_fn rm $files

test ()
{
    nbdkit -v -D extentlist.lookup=1 \
           -r -U - \
           --filter=extentlist \
           null size=$1 extentlist=$input \
           --run 'qemu-img map -f raw --output=json $nbd' |
        jq -c '.[] | {start:.start, length:.length, data:.data, zero:.zero}' \
           > $out
    diff -u $out $expected
}

# Empty extent list.
cat > $input <<'EOF'
EOF

cat > $expected <<'EOF'
{"start":0,"length":0,"data":false,"zero":false}
EOF
test 0
cat > $expected <<'EOF'
{"start":0,"length":1048576,"data":false,"zero":true}
EOF
test 1M

# Extent list covering 0-1M with data.
cat > $input <<'EOF'
0 1M
EOF

cat > $expected <<'EOF'
{"start":0,"length":0,"data":false,"zero":false}
EOF
test 0
cat > $expected <<'EOF'
{"start":0,"length":1048576,"data":true,"zero":false}
EOF
test 1M

# Extent list covering 1-2M with data.
cat > $input <<'EOF'
1M 1M
EOF

cat > $expected <<'EOF'
{"start":0,"length":0,"data":false,"zero":false}
EOF
test 0
cat > $expected <<'EOF'
{"start":0,"length":1048576,"data":false,"zero":true}
EOF
test 1M
cat > $expected <<'EOF'
{"start":0,"length":1048576,"data":false,"zero":true}
{"start":1048576,"length":1048576,"data":true,"zero":false}
EOF
test 2M
cat > $expected <<'EOF'
{"start":0,"length":1048576,"data":false,"zero":true}
{"start":1048576,"length":1048576,"data":true,"zero":false}
{"start":2097152,"length":1048576,"data":false,"zero":true}
EOF
test 3M

# Extent list covering 1-2M with data, but in a more fragmented
# way than the above.
cat > $input <<'EOF'
1024K 512K
1536K 512K
EOF

cat > $expected <<'EOF'
{"start":0,"length":0,"data":false,"zero":false}
EOF
test 0
cat > $expected <<'EOF'
{"start":0,"length":1048576,"data":false,"zero":true}
EOF
test 1M
cat > $expected <<'EOF'
{"start":0,"length":1048576,"data":false,"zero":true}
{"start":1048576,"length":1048576,"data":true,"zero":false}
EOF
test 2M
cat > $expected <<'EOF'
{"start":0,"length":1048576,"data":false,"zero":true}
{"start":1048576,"length":1048576,"data":true,"zero":false}
{"start":2097152,"length":1048576,"data":false,"zero":true}
EOF
test 3M

# Adjacent data and holes.
cat > $input <<'EOF'
0 1M
2M 1M
4M 1M
EOF

cat > $expected <<'EOF'
{"start":0,"length":0,"data":false,"zero":false}
EOF
test 0
cat > $expected <<'EOF'
{"start":0,"length":1048576,"data":true,"zero":false}
EOF
test 1M
cat > $expected <<'EOF'
{"start":0,"length":1048576,"data":true,"zero":false}
{"start":1048576,"length":1048576,"data":false,"zero":true}
EOF
test 2M
cat > $expected <<'EOF'
{"start":0,"length":1048576,"data":true,"zero":false}
{"start":1048576,"length":1048576,"data":false,"zero":true}
{"start":2097152,"length":1048576,"data":true,"zero":false}
EOF
test 3M
cat > $expected <<'EOF'
{"start":0,"length":1048576,"data":true,"zero":false}
{"start":1048576,"length":1048576,"data":false,"zero":true}
{"start":2097152,"length":1048576,"data":true,"zero":false}
{"start":3145728,"length":1048576,"data":false,"zero":true}
{"start":4194304,"length":1048576,"data":true,"zero":false}
{"start":5242880,"length":1048576,"data":false,"zero":true}
EOF
test 6M
