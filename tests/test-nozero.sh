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

source ./functions.sh
set -e

sock1=`mktemp -u`
sock2=`mktemp -u`
sock3=`mktemp -u`
sock4=`mktemp -u`
sock5a=`mktemp -u`
sock5b=`mktemp -u`
files="nozero1.img nozero1.log $sock1 nozero1.pid
       nozero2.img nozero2.log $sock2 nozero2.pid
       nozero3.img nozero3.log $sock3 nozero3.pid
       nozero4.img nozero4.log $sock4 nozero4.pid
       nozero5.img nozero5a.log nozero5b.log $sock5a $sock5b
       nozero5a.pid nozero5b.pid"
rm -f $files

# Prep images, and check that qemu-io understands the actions we plan on
# doing, and that zero with trim results in a sparse image.
for f in {0..1023}; do printf '%1024s' . >> nozero1.img; done
cp nozero1.img nozero2.img
cp nozero1.img nozero3.img
cp nozero1.img nozero4.img
cp nozero1.img nozero5.img
if ! qemu-io -f raw -d unmap -c 'w -z -u 0 1M' nozero1.img; then
    echo "$0: missing or broken qemu-io"
    rm nozero?.img
    exit 77
fi
if test "$(stat -c %b nozero1.img)" = "$(stat -c %b nozero2.img)"; then
    echo "$0: can't trim file by writing zeroes"
    rm nozero?.img
    exit 77
fi
cp nozero2.img nozero1.img

# For easier debugging, dump the final log files before removing them
# on exit.
cleanup ()
{
    echo "Log 1 file contents:"
    cat nozero1.log || :
    echo "Log 2 file contents:"
    cat nozero2.log || :
    echo "Log 3 file contents:"
    cat nozero3.log || :
    echo "Log 4 file contents:"
    cat nozero4.log || :
    echo "Log 5a file contents:"
    cat nozero5a.log || :
    echo "Log 5b file contents:"
    cat nozero5b.log || :
    rm -f $files
}
cleanup_fn cleanup

# Run four parallel nbdkit; to compare the logs and see what changes.
# 1: unfiltered, to check that qemu-io sends ZERO request and plugin trims
# 2: log before filter with zeromode=none (default), to ensure no ZERO request
# 3: log before filter with zeromode=emulate, to ensure ZERO from client
# 4: log after filter with zeromode=emulate, to ensure no ZERO to plugin
# 5a/b: both sides of nbd plugin: even though server side does not advertise
# ZERO, the client side still exposes it, and just skips calling nbd's .zero
start_nbdkit -P nozero1.pid -U $sock1 --filter=log \
       file logfile=nozero1.log nozero1.img
start_nbdkit -P nozero2.pid -U $sock2 --filter=log --filter=nozero \
       file logfile=nozero2.log nozero2.img
start_nbdkit -P nozero3.pid -U $sock3 --filter=log --filter=nozero \
       file logfile=nozero3.log nozero3.img zeromode=emulate
start_nbdkit -P nozero4.pid -U $sock4 --filter=nozero --filter=log \
       file logfile=nozero4.log nozero4.img zeromode=emulate
start_nbdkit -P nozero5a.pid -U $sock5a --filter=log --filter=nozero \
       file logfile=nozero5a.log nozero5.img
start_nbdkit -P nozero5b.pid -U $sock5b --filter=log \
       nbd logfile=nozero5b.log socket=$sock5a

# Perform the zero write.
qemu-io -f raw -c 'w -z -u 0 1M' "nbd+unix://?socket=$sock1"
qemu-io -f raw -c 'w -z -u 0 1M' "nbd+unix://?socket=$sock2"
qemu-io -f raw -c 'w -z -u 0 1M' "nbd+unix://?socket=$sock3"
qemu-io -f raw -c 'w -z -u 0 1M' "nbd+unix://?socket=$sock4"
qemu-io -f raw -c 'w -z -u 0 1M' "nbd+unix://?socket=$sock5b"

# Check for expected ZERO vs. WRITE results
grep 'connection=1 Zero' nozero1.log
if grep 'connection=1 Zero' nozero2.log; then
    echo "filter should have prevented zero"
    exit 1
fi
grep 'connection=1 Zero' nozero3.log
if grep 'connection=1 Zero' nozero4.log; then
    echo "filter should have converted zero into write"
    exit 1
fi
grep 'connection=1 Zero' nozero5b.log
if grep 'connection=1 Zero' nozero5a.log; then
    echo "nbdkit should have converted zero into write before nbd plugin"
    exit 1
fi

# Sanity check on contents - all 5 files should read identically
cmp nozero1.img nozero2.img
cmp nozero2.img nozero3.img
cmp nozero3.img nozero4.img
cmp nozero4.img nozero5.img
