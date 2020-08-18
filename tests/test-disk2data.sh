#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2020 Red Hat Inc.
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

# Test the plugins/data/disk2data.pl script.
#
# This is a stochastic test to try to find corner cases.  However if
# we simply used a random disk then we wouldn't be testing the
# particular features of this script, namely that it can skip long
# runs of zeroes and compress short-period repeated data.  So instead
# we have a small script which generates this kind of "disk-like"
# data.

source ./functions.sh
set -e
set -x

requires_unix_domain_sockets
requires perl --version
requires python3 --version
requires python3 -c 'import random'
requires nbdcopy --version
requires hexdump --version

disk2data=$srcdir/../plugins/data/disk2data.pl
requires test -x $disk2data

disk=test-disk2data.disk
cmd=test-disk2data.cmd
out=test-disk2data.out
cleanup_fn rm -f $disk $cmd $out
rm -f $disk $cmd $out

export disk=$disk
python3 -c '
import os
import random

pop = []
# Zeroes.
pop.append([0]*3)
pop.append([0]*10)
pop.append([0]*10)
pop.append([0]*10)
pop.append([0]*50)
pop.append([0]*100)
pop.append([0]*200)
pop.append([0]*300)
pop.append([0]*500)
pop.append([0]*1000)
pop.append([0]*10000)
# Non-zero periodic sequences.
pop.append([1]*10)
pop.append([1,2]*10)
pop.append([1,2,3]*10)
pop.append([1,2,3,4]*10)
pop.append([1,2,3,4,5]*10)
pop.append([1,2,3,4,5,6]*10)
pop.append([1,2,3,4,5,6,7]*10)
pop.append([1,2,3,4,5,6,7,8]*10)
pop.append([1,2,3,4,5,6,7,8,9,10]*10) # too long for the script to detect
# Random non-repeating data sequences.
pop.append([1,4,5,2,9,8,3,5,3,1,3,4,5])
pop.append([9,7,5,3,1,2,4,6,8])

len = random.randint(0, 20)
choices = random.choices(pop, k=len)
with open(os.environ["disk"], "w") as f:
    for i in choices:
        f.write(bytearray(i).decode("ascii"))
'
hexdump -C $disk

# Run the script to convert the disk to an nbdkit data command.
$disk2data $disk > $cmd

# Modify the generated nbdkit command.
sed -i -e $'s/^nbdkit /nbdkit -U - --run \'nbdcopy "$uri" -\' /' $cmd
chmod +x $cmd
cat $cmd

# Run the command.  It should re-generate the original disk image.
./$cmd > $out
hexdump -C $out

# Compare the original disk with the output.
cmp $disk $out
