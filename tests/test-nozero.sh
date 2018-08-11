#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018 Red Hat Inc.
# All rights reserved.
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

set -e

files="nozero1.img nozero1.log nozero1.sock nozero1.pid
       nozero2.img nozero2.log nozero2.sock nozero2.pid
       nozero3.img nozero3.log nozero3.sock nozero3.pid
       nozero4.img nozero4.log nozero4.sock nozero4.pid
       nozero5.img nozero5a.log nozero5b.log nozero5a.sock nozero5b.sock
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
    echo "$0: can't trim file by writing zeros"
    rm nozero?.img
    exit 77
fi
cp nozero2.img nozero1.img

pid1= pid2= pid3= pid4= pid5a= pid5b=

# Kill any nbdkit processes on exit.
cleanup ()
{
    status=$?
    trap '' INT QUIT TERM EXIT ERR
    echo $0: cleanup: exit code $status

    test "$pid1" && kill $pid1
    test "$pid2" && kill $pid2
    test "$pid3" && kill $pid3
    test "$pid4" && kill $pid4
    test "$pid5a" && kill $pid5a
    test "$pid5b" && kill $pid5b
    # For easier debugging, dump the final log files before removing them.
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

    exit $status
}
trap cleanup INT QUIT TERM EXIT ERR

# Run four parallel nbdkit; to compare the logs and see what changes.
# 1: unfiltered, to check that qemu-io sends ZERO request and plugin trims
# 2: log before filter with zeromode=none (default), to ensure no ZERO request
# 3: log before filter with zeromode=emulate, to ensure ZERO from client
# 4: log after filter with zeromode=emulate, to ensure no ZERO to plugin
# 5a/b: both sides of nbd plugin: even though server side does not advertise
# ZERO, the client side still exposes it, and just skips calling nbd's .zero
nbdkit -P nozero1.pid -U nozero1.sock --filter=log \
       file logfile=nozero1.log file=nozero1.img
nbdkit -P nozero2.pid -U nozero2.sock --filter=log --filter=nozero \
       file logfile=nozero2.log file=nozero2.img
nbdkit -P nozero3.pid -U nozero3.sock --filter=log --filter=nozero \
       file logfile=nozero3.log file=nozero3.img zeromode=emulate
nbdkit -P nozero4.pid -U nozero4.sock --filter=nozero --filter=log \
       file logfile=nozero4.log file=nozero4.img zeromode=emulate
nbdkit -P nozero5a.pid -U nozero5a.sock --filter=log --filter=nozero \
       file logfile=nozero5a.log file=nozero5.img
nbdkit -P nozero5b.pid -U nozero5b.sock --filter=log \
       nbd logfile=nozero5b.log socket=nozero5a.sock

# We may have to wait a short time for the pid files to appear.
for i in `seq 1 10`; do
    if test -f nozero1.pid && test -f nozero2.pid && test -f nozero3.pid &&
       test -f nozero4.pid && test -f nozero5a.pid && test -f nozero5b.pid; then
        break
    fi
    sleep 1
done

pid1="$(cat nozero1.pid)" || :
pid2="$(cat nozero2.pid)" || :
pid3="$(cat nozero3.pid)" || :
pid4="$(cat nozero4.pid)" || :
pid5a="$(cat nozero5a.pid)" || :
pid5b="$(cat nozero5b.pid)" || :

if ! test -f nozero1.pid || ! test -f nozero2.pid || ! test -f nozero3.pid ||
   ! test -f nozero4.pid || ! test -f nozero5a.pid || ! test -f nozero5b.pid; then
    echo "$0: PID files were not created"
    exit 1
fi

# Perform the zero write.
qemu-io -f raw -c 'w -z -u 0 1M' 'nbd+unix://?socket=nozero1.sock'
qemu-io -f raw -c 'w -z -u 0 1M' 'nbd+unix://?socket=nozero2.sock'
qemu-io -f raw -c 'w -z -u 0 1M' 'nbd+unix://?socket=nozero3.sock'
qemu-io -f raw -c 'w -z -u 0 1M' 'nbd+unix://?socket=nozero4.sock'
qemu-io -f raw -c 'w -z -u 0 1M' 'nbd+unix://?socket=nozero5b.sock'

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

# The cleanup() function is called implicitly on exit.
