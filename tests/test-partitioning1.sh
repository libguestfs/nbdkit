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

# Test the partitioning plugin.
#
# Test 1: check that partitioning + partition filter = identity

source ./functions.sh
set -e
set -x

requires_unix_domain_sockets
requires qemu-img --version

files="partitioning1.out partitioning1-p1 partitioning1-p2 partitioning1-p3 partitioning1-p4 partitioning1-p5 partitioning1-p6"
rm -f $files
cleanup_fn rm -f $files

# Create some odd-sized partitions.  These exist to test alignment and
# padding.
truncate -s 1 partitioning1-p1
truncate -s 511 partitioning1-p2
truncate -s 10M partitioning1-p3
truncate -s 1023 partitioning1-p4
truncate -s 1 partitioning1-p5
truncate -s 511 partitioning1-p6

# Run nbdkit with partitioning plugin and partition filter.
nbdkit -f -v -D partitioning.regions=1 -U - \
       --filter=partition \
       partitioning \
       mbr-id=0x83 alignment=512 \
       partitioning1-p1 \
       mbr-id=0x82 alignment=$((2048 * 512)) \
       partitioning1-p2 \
       mbr-id=default \
       file-data \
       mbr-id=0x82 \
       partitioning1-p3 \
       partition-type=mbr \
       partition=3 \
       --run 'qemu-img convert $nbd partitioning1.out'

# Contents of partitioning1.out should be identical to file-data.
cmp file-data partitioning1.out

# Same test with > 4 MBR partitions.
# Note we select partition 6 because partition 4 is the extended partition.
nbdkit -f -v -D partitioning.regions=1 -U - \
       --filter=partition \
       partitioning \
       partitioning1-p1 \
       partitioning1-p2 \
       partitioning1-p3 \
       partitioning1-p4 \
       file-data \
       partitioning1-p5 \
       partitioning1-p6 \
       partition-type=mbr \
       partition=6 \
       --run 'qemu-img convert $nbd partitioning1.out'

cmp file-data partitioning1.out

# Same test with GPT.
nbdkit -f -v -D partitioning.regions=1 -U - \
       --filter=partition \
       partitioning \
       partitioning1-p1 \
       partitioning1-p2 \
       partitioning1-p3 \
       partitioning1-p4 \
       type-guid=A2A0D0EB-E5B9-3344-87C0-68B6B72699C7 \
       file-data \
       type-guid=AF3DC60F-8384-7247-8E79-3D69D8477DE4 \
       partitioning1-p5 \
       partitioning1-p6 \
       partition-type=gpt \
       partition=5 \
       --run 'qemu-img convert $nbd partitioning1.out'

cmp file-data partitioning1.out
