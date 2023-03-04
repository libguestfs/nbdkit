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
# Test 2: Create a naked filesystem, embed in a partition, and try to
# read/write it with guestfish.

source ./functions.sh
set -e
set -x

requires guestfish --version
requires mke2fs -V

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="partitioning2.pid $sock partitioning2.fs partitioning2.p1 partitioning2.p3"
rm -f $files
cleanup_fn rm -f $files

# Create partitions before and after.
$TRUNCATE -s 1 partitioning2.p1
$TRUNCATE -s 10M partitioning2.p3

# Create the naked filesystem.
$TRUNCATE -s 20M partitioning2.fs
mke2fs -F -t ext2 partitioning2.fs

# Run nbdkit.
start_nbdkit -P partitioning2.pid -U $sock \
             partitioning partitioning2.p1 partitioning2.fs partitioning2.p3 \
             partition-type=gpt

# Connect with guestfish and read/write stuff to partition 2.
guestfish --format=raw -a "nbd://?socket=$sock" <<'EOF'
  run
  mount /dev/sda2 /
  touch /hello
  fill-pattern "abc" 10000 /pattern
  ll /
  umount /dev/sda2
  sync
EOF
