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

requires_nbdinfo
requires test -f disk

files="partition2.log"
rm -f $files
cleanup_fn rm -f $files

# Select partition 3 from a 2 partition disk.
nbdkit -U - -r -f -v --filter=partition \
         partitioning disk disk partition-type=mbr partition=3 \
         --run 'nbdinfo $nbd' > partition2.log 2>&1 ||:

cat partition2.log

grep "MBR partition 3 not found" partition2.log

# Selection partition 11 from a 9 partition disk.  NB: partition 4 is
# the extended partition so it is skipped.  This test is slightly
# different from above as it invokes the code supporting logical
# partitions.
nbdkit -U - -r -f -v --filter=partition \
         partitioning disk disk disk disk disk disk disk disk disk \
         partition-type=mbr partition=11 \
         --run 'nbdinfo $nbd' > partition2.log 2>&1 ||:

cat partition2.log

grep "MBR partition 11 not found" partition2.log

# It should be impossible to select an extended partition.
nbdkit -U - -r -f -v --filter=partition \
         partitioning disk disk disk disk disk partition-type=mbr partition=4 \
         --run 'nbdinfo $nbd' > partition2.log 2>&1 ||:

cat partition2.log

grep "MBR partition 4 not found" partition2.log

# Selecting a logical partition on a disk without an extended
# partition gives a different error.
nbdkit -U - -r -f -v --filter=partition \
         partitioning disk disk partition-type=mbr partition=5 \
         --run 'nbdinfo $nbd' > partition2.log 2>&1 ||:

cat partition2.log

grep "there is no extended partition" partition2.log
