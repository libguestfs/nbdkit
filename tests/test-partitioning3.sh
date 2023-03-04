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

# Test the partitioning plugin.
#
# Test 3: Check GUIDs.

source ./functions.sh
set -e
set -x

requires guestfish --version

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="partitioning3.pid $sock partitioning3.p1 partitioning3.p2 partitioning3.p3 partitioning3.out"
rm -f $files
cleanup_fn rm -f $files

# Create the partitions.
$TRUNCATE -s 1 partitioning3.p1
$TRUNCATE -s 10M partitioning3.p2
$TRUNCATE -s 100M partitioning3.p3

# Run nbdkit.
start_nbdkit -P partitioning3.pid -U $sock \
             partitioning \
             partitioning3.p1 \
             type-guid=default \
             partitioning3.p2 \
             type-guid=0657FD6D-A4AB-43C4-84E5-0933C84B4F4F \
             partitioning3.p3 \
             partition-type=gpt

# Connect with guestfish and read partition types.
guestfish --ro --format=raw -a "nbd://?socket=$sock" > partitioning3.out <<'EOF'
  run
  part-get-gpt-type /dev/sda 1
  part-get-gpt-type /dev/sda 2
  part-get-gpt-type /dev/sda 3
EOF

if [ "$(cat partitioning3.out)" != "0FC63DAF-8483-4772-8E79-3D69D8477DE4
0FC63DAF-8483-4772-8E79-3D69D8477DE4
0657FD6D-A4AB-43C4-84E5-0933C84B4F4F" ]; then
    echo "$0: unexpected partition type GUIDs:"
    cat partitioning3.out
    exit 1
fi
