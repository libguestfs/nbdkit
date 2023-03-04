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
# Test 5: Create a filesystem and embed it in an MBR logical
# partition.  libguestfs uses virtio-scsi so the practical limit here
# is about 15 partitions.

source ./functions.sh
set -e
set -x

requires guestfish --version
requires mke2fs -V

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="partitioning5.pid $sock
       partitioning5.fs
       partitioning5.p1 partitioning5.p2 partitioning5.p3 partitioning5.p5 partitioning5.p6 partitioning5.p7 partitioning5.p8 partitioning5.p9 partitioning5.p10 partitioning5.p11 partitioning5.p13"
rm -f $files
cleanup_fn rm -f $files

# Create partitions before and after.
$TRUNCATE -s 1 partitioning5.p1
$TRUNCATE -s 10M partitioning5.p2
$TRUNCATE -s 512 partitioning5.p3
# partition 4 = extended partition
$TRUNCATE -s 1 partitioning5.p5
$TRUNCATE -s 512 partitioning5.p6
$TRUNCATE -s 1 partitioning5.p7
$TRUNCATE -s 1 partitioning5.p8
$TRUNCATE -s 10M partitioning5.p9
$TRUNCATE -s 512 partitioning5.p10
$TRUNCATE -s 1 partitioning5.p11
# partition 12 = naked filesystem
$TRUNCATE -s 10M partitioning5.p13

# Create the naked filesystem.
$TRUNCATE -s 20M partitioning5.fs
mke2fs -F -t ext2 partitioning5.fs

# Run nbdkit.
start_nbdkit -P partitioning5.pid -U $sock \
             partitioning \
             partitioning5.p1 partitioning5.p2 \
             partitioning5.p3 \
             partitioning5.p5 partitioning5.p6 \
             partitioning5.p7 partitioning5.p8 \
             partitioning5.p9 partitioning5.p10 \
             partitioning5.p11 partitioning5.fs \
             partitioning5.p13 \
             partition-type=mbr

# Connect with guestfish and read/write stuff to partition 12.
guestfish --format=raw -a "nbd://?socket=$sock" <<'EOF'
  run
  mount /dev/sda12 /
  touch /hello
  fill-pattern "abc" 10000 /pattern
  ll /
  umount /dev/sda12
  sync
EOF
