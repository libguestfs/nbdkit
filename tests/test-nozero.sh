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

source ./functions.sh
set -e
set -x

requires $STAT --version

sock2=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
sock3=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
sock4a=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
sock4b=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
sock5=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="nozero1.img nozero1.log
       nozero2.img nozero2.log $sock2 nozero2.pid
       nozero3.img nozero3.log $sock3 nozero3.pid
       nozero4.img nozero4a.log nozero4b.log $sock4a $sock4b
       nozero4a.pid nozero4b.pid
       nozero5.img nozero5.log $sock5 nozero5.pid"
rm -f $files
fail=0

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
    echo "Log 4a file contents:"
    cat nozero4a.log || :
    echo "Log 4b file contents:"
    cat nozero4b.log || :
    echo "Log 5 file contents:"
    cat nozero5.log || :
    rm -f $files
}
cleanup_fn cleanup

# Prep images.
declare -a sizes
printf %$((2*1024*1024))s . > nozero1.img
cp nozero1.img nozero2.img
cp nozero1.img nozero3.img
cp nozero1.img nozero4.img
cp nozero1.img nozero5.img

# Debug number of blocks and block size in the images.
for f in {1..5}; do
    $STAT -c "%n: %b allocated blocks of size %B bytes, total size %s" \
         nozero$f.img
    sizes[$f]=$($STAT -c %b nozero$f.img)
done

# Check that zero with trim results in a sparse image.
requires nbdkit -U - --filter=log file logfile=nozero1.log nozero1.img \
    --run 'nbdsh -u "$uri" -c "h.zero(1024*1024, 0)"'
if test "$($STAT -c %b nozero1.img)" = "${sizes[1]}"; then
    echo "$0: can't trim file by writing zeroes"
    exit 77
fi

# Run several parallel nbdkit; to compare the logs and see what changes.
# 1: unfiltered (above), check that nbdsh sends ZERO request and plugin trims
# 2: log before filter with zeromode=none (default), to ensure no ZERO request
# 3: log after filter with zeromode=emulate, to ensure no ZERO to plugin
# 4a/b: both sides of nbd plugin: even though server side does not advertise
# ZERO, the client side still exposes it, and just skips calling nbd's .zero
# 5: log after filter with zeromode=notrim, to ensure plugin does not trim
start_nbdkit -P nozero2.pid -U $sock2 --filter=log --filter=nozero \
       file logfile=nozero2.log nozero2.img
start_nbdkit -P nozero3.pid -U $sock3 --filter=nozero --filter=log \
       file logfile=nozero3.log nozero3.img zeromode=emulate
# Start 4b before 4a so that cleanup visits the client before the server
start_nbdkit -P nozero4b.pid -U $sock4b --filter=log \
       nbd logfile=nozero4b.log socket=$sock4a
start_nbdkit -P nozero4a.pid -U $sock4a --filter=log --filter=nozero \
       file logfile=nozero4a.log nozero4.img
start_nbdkit -P nozero5.pid -U $sock5 --filter=nozero --filter=log \
       file logfile=nozero5.log nozero5.img zeromode=notrim

# Perform the zero write.
nbdsh -u "nbd+unix://?socket=$sock2" -c '
assert not h.can_zero()
h.pwrite (bytearray(1024*1024), 0)
'
nbdsh -u "nbd+unix://?socket=$sock3" -c 'h.zero(1024*1024, 0)'
nbdsh -u "nbd+unix://?socket=$sock4b" -c 'h.zero(1024*1024, 0)'
nbdsh -u "nbd+unix://?socket=$sock5" -c 'h.zero(1024*1024, 0)'

# Check for expected ZERO vs. WRITE results
grep 'connection=1 Zero' nozero1.log || fail=1
if grep 'connection=1 Zero' nozero2.log; then
    echo "filter should have prevented zero"
    fail=1
fi
if grep 'connection=1 Zero' nozero3.log; then
    echo "filter should have converted zero into write"
    fail=1
fi
if grep 'connection=1 Zero' nozero4a.log; then
    echo "nbdkit should have converted zero into write before nbd plugin"
    fail=1
fi
grep 'connection=1 Zero' nozero5.log

# Sanity check on contents - all 5 files should read identically
cmp nozero1.img nozero2.img
cmp nozero2.img nozero3.img
cmp nozero3.img nozero4.img
cmp nozero4.img nozero5.img

# Sanity check on sparseness: images 2-5 should not be sparse (although the
# filesystem may have reserved additional space due to our writes)
for i in {2..5}; do
    if test "$($STAT -c %b nozero$i.img)" -lt "${sizes[$i]}"; then
        echo "nozero$i.img was trimmed by mistake"
        fail=1
    fi
done

exit $fail
