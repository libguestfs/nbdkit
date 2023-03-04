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

requires_filter log
requires qemu-io --version

sock1=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
sock2=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="blocksize1.img blocksize1.log $sock1 blocksize1.pid
       blocksize2.img blocksize2.log $sock2 blocksize2.pid"
rm -f $files

# Prep images, and check that qemu-io understands the actions we plan on doing.
$TRUNCATE -s 10M blocksize1.img
if ! qemu-io -f raw -c 'r 0 1' -c 'w -z 1000 2000' \
     -c 'w -P 0 1M 2M' -c 'discard 3M 4M' blocksize1.img; then
    echo "$0: missing or broken qemu-io"
    rm blocksize1.img
    exit 77
fi
$TRUNCATE -s 10M blocksize2.img

# For easier debugging, dump the final log files before removing them
# on exit.
cleanup ()
{
    echo "Log 1 file contents:"
    cat blocksize1.log || :
    echo "Log 2 file contents:"
    cat blocksize2.log || :
    rm -f $files
}
cleanup_fn cleanup

# Run two parallel nbdkit; to compare the logs and see what changes.
start_nbdkit -P blocksize1.pid -U $sock1 \
       --filter=log file logfile=blocksize1.log blocksize1.img
start_nbdkit -P blocksize2.pid -U $sock2 --filter=blocksize \
       --filter=log file logfile=blocksize2.log blocksize2.img \
       minblock=1024 maxdata=512k maxlen=1M

# Test behavior on short accesses.
qemu-io -f raw -c 'r 1 1' -c 'w 10001 1' -c 'w -z 20001 1' \
         -c 'discard 30001 1' "nbd+unix://?socket=$sock1"
qemu-io -f raw -c 'r 1 1' -c 'w 10001 1' -c 'w -z 20001 1' \
         -c 'discard 30001 1' "nbd+unix://?socket=$sock2"

# Read should round up (qemu-io may round to 512, but we must round to 1024
grep 'connection=1 Read .* count=0x\(1\|200\) ' blocksize1.log ||
    { echo "qemu-io can't pass 1-byte reads"; exit 77; }
grep 'connection=1 Read .* offset=0x0 count=0x400 ' blocksize2.log
# Write should become read-modify-write
grep 'connection=1 Write .* count=0x\(1\|200\) ' blocksize1.log ||
    { echo "qemu-io can't pass 1-byte writes"; exit 77; }
grep 'connection=1 Read .* offset=0x2400 count=0x400 ' blocksize2.log
grep 'connection=1 Write .* offset=0x2400 count=0x400 ' blocksize2.log
# Zero should become read-modify-write
if grep 'connection=1 Zero' blocksize2.log; then
    echo "filter should have converted short zero to write"
    exit 1
fi
grep 'connection=1 Read .* offset=0x4c00 count=0x400 ' blocksize2.log
grep 'connection=1 Write .* offset=0x4c00 count=0x400 ' blocksize2.log
# Trim should be discarded
if grep 'connection=1 Trim' blocksize2.log; then
    echo "filter should have dropped too-small trim"
    exit 1
fi

# Test behavior on overlarge accesses.
qemu-io -f raw -c 'w -P 11 1048575 4094305' -c 'w -z 1050000 1100000' \
         -c 'r -P 0 1050000 1100000' -c 'r -P 11 3000000 1048577' \
         -c 'discard 7340031 2097153' "nbd+unix://?socket=$sock1"
qemu-io -f raw -c 'w -P 11 1048575 4094305' -c 'w -z 1050000 1100000' \
         -c 'r -P 0 1050000 1100000' -c 'r -P 11 3000000 1048577' \
         -c 'discard 7340031 2097153' "nbd+unix://?socket=$sock2"

# Reads and writes should have been split.
test "$(grep -c '\(Read\|Write\) .*count=0x80000 ' blocksize2.log)" -ge 10
test "$(grep -c '\(Read\|Write\) .*count=0x[0-9a-f]\{6\} ' blocksize2.log)" = 0
# Zero and trim should be split, but at different boundary
grep 'Zero .*count=0x100000 ' blocksize2.log
test "$(grep -c 'connection=2 Zero' blocksize2.log)" = 2
if grep Trim blocksize1.log; then
    test "$(grep -c 'connection=2 Trim .*count=0x100000 ' blocksize2.log)" = 2
fi

# Final sanity checks.
if grep 'offset=0x[0-9a-f]*\([1235679abdef]00\|[0-9a-f]\(.[^0]\|[^0].\)\) ' \
        blocksize2.log; then
    echo "filter didn't align offset to 1024";
    exit 1;
fi
if grep 'count=0x[0-9a-f]*\([1235679abdef]00\|[0-9a-f]\(.[^0]\|[^0].\)\) ' \
        blocksize2.log; then
    echo"filter didn't align count to 512";
    exit 1;
fi
diff -u blocksize1.img blocksize2.img
