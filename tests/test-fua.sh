#!/bin/bash -
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
set -x

files="fua.img
       fua1.log fua1.sock fua1.pid
       fua2.log fua2.sock fua2.pid
       fua3.log fua3.sock fua3.pid
       fua4.log fua4.sock fua4.pid"
rm -f $files

# Prep images, and check that qemu-io understands the actions we plan on
# doing.  We can't test trim+FUA, since qemu-io won't expose that.
truncate --size 1M fua.img
if ! qemu-io -f raw -t none -c flush -c 'w -f -z 0 64k' fua.img; then
    echo "$0: missing or broken qemu-io"
    rm fua.img
    exit 77
fi

pid1= pid2= pid3= pid4=

# Kill any nbdkit processes on exit.
cleanup ()
{
    status=$?

    test "$pid1" && kill $pid1
    test "$pid2" && kill $pid2
    test "$pid3" && kill $pid3
    test "$pid4" && kill $pid4
    # For easier debugging, dump the final log files before removing them.
    echo "Log 1 file contents:"
    cat fua1.log || :
    echo "Log 2 file contents:"
    cat fua2.log || :
    echo "Log 3 file contents:"
    cat fua3.log || :
    echo "Log 4 file contents:"
    cat fua4.log || :
    rm -f $files

    exit $status
}
trap cleanup INT QUIT TERM EXIT ERR

# Run four parallel nbdkit; to compare the logs and see what changes.
# 1: fuamode=none (default): client should send flush instead
# 2: fuamode=emulate: log shows that blocksize optimizes fua to flush
# 3: fuamode=native: log shows that blocksize preserves fua
# 4: fuamode=force: log shows that fua is always enabled
nbdkit -P fua1.pid -U fua1.sock --filter=log --filter=fua \
       file logfile=fua1.log file=fua.img
nbdkit -P fua2.pid -U fua2.sock --filter=blocksize --filter=log --filter=fua \
       file logfile=fua2.log file=fua.img fuamode=emulate maxdata=4k maxlen=4k
nbdkit -P fua3.pid -U fua3.sock --filter=blocksize --filter=log --filter=fua \
       file logfile=fua3.log file=fua.img fuamode=native maxdata=4k maxlen=4k
nbdkit -P fua4.pid -U fua4.sock --filter=fua --filter=log \
       file logfile=fua4.log file=fua.img fuamode=force

# We may have to wait a short time for the pid files to appear.
for i in `seq 1 10`; do
    if test -f fua1.pid && test -f fua2.pid && test -f fua3.pid &&
       test -f fua4.pid; then
        break
    fi
    sleep 1
done

pid1="$(cat fua1.pid)" || :
pid2="$(cat fua2.pid)" || :
pid3="$(cat fua3.pid)" || :
pid4="$(cat fua4.pid)" || :

if ! test -f fua1.pid || ! test -f fua2.pid || ! test -f fua3.pid ||
   ! test -f fua4.pid; then
    echo "$0: PID files were not created"
    exit 1
fi

# Perform a flush, write, and zero, first without then with FUA
for f in '' -f; do
    for i in {1..4}; do
	qemu-io -f raw -t none -c flush -c "w $f 0 64k" -c "w -z $f 64k 64k" \
		 "nbd+unix://?socket=fua$i.sock"
    done
done

# Test 1: no fua sent over wire, qemu-io sent more flushes in place of fua
if grep 'fua=1' fua1.log; then
    echo "filter should have prevented fua"
    exit 1
fi
test $(grep -c 'connection=1 Flush' fua1.log) -lt \
     $(grep -c 'connection=2 Flush' fua1.log)

# Test 2: either last part of split has fua, or a flush is added, but
# all earlier parts of the transaction do not have fua
flush1=$(grep -c 'connection=1 Flush' fua2.log || :)
flush2=$(grep -c 'connection=2 Flush' fua2.log || :)
fua=$(grep -c 'connection=2.*fua=1' fua2.log || :)
test $(( $flush2 - $flush1 + $fua )) = 2

# Test 3: every part of split has fua, and no flushes are added
flush1=$(grep -c 'connection=1 Flush' fua3.log || :)
flush2=$(grep -c 'connection=2 Flush' fua3.log || :)
test $flush1 = $flush2
test $(grep -c 'connection=2.*fua=1' fua3.log) = 32

# Test 4: flush is no-op, and every transaction has fua
if grep 'fua=0' fua4.log; then
    echo "filter should have forced fua"
    exit 1
fi
if grep 'Flush' fua4.log; then
    echo "filter should have elided flush"
    exit 1
fi

# The cleanup() function is called implicitly on exit.
