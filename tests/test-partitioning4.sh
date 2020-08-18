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
# Test 4: Test > 128 partitions using GPT.
#
# virtio-scsi (used by libguestfs) doesn't support more than 15
# partitions.  In fact the only client which supports this is our own
# partition filter so we use that for the test.

source ./functions.sh
set -e
set -x

requires_unix_domain_sockets
requires qemu-img --version

# This test requires the partitioning plugin to open at least 768
# files (say 800 to make it a round number).  On OpenBSD the limit on
# open files is set to 512 and so the test fails.
if [ $(ulimit -n) -lt 800 ]; then
    echo "$0: ulimit open files is too low for this test"
    exit 77
fi

d=partitioning4.d
rm -rf $d
mkdir $d
cleanup_fn rm -rf $d

# Create the partitions.
for i in {1..768}; do
    truncate -s 1 $(printf '%s/part.%04d' $d $i)
done

# Create partition 250 containing data and truncate it to a whole
# number of sectors.
rm $d/part.0250
for i in {0..1000}; do
    printf "hello " >> $d/part.0250
done
truncate -s 6144 $d/part.0250

# Run nbdkit.
nbdkit -f -v -D partitioning.regions=1 -U - \
             --filter=partition \
             partitioning \
             $d/part.* \
             partition-type=gpt \
             partition=250 \
             --run "qemu-img convert \$nbd $d/out"

# The output should be identical to partition 250.
cmp $d/part.0250 $d/out
