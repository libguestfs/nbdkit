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

requires_filter fua
requires_filter log

sockdir=$(mktemp -d /tmp/nbdkit-test-socks.XXXXXX)
files="fua.img
       fua1.log fua1.pid
       fua2.log fua2.pid
       fua3.log fua3.pid
       fua4.log fua4.pid
       fua5.log fua5.pid
       fua6.log fua6.pid"
rm -f $files

# Prep images, and check that qemu-io understands the actions we plan on
# doing.  We can't test trim+FUA, since qemu-io won't expose that.
$TRUNCATE -s 1M fua.img
if ! qemu-io -f raw -t none -c flush -c 'w -f -z 0 64k' fua.img; then
    echo "$0: missing or broken qemu-io"
    rm fua.img
    exit 77
fi

# For easier debugging, dump the final log files before removing them
# on exit.
cleanup ()
{
    for i in {1..6}; do
        echo "Log $i file contents:"
        cat fua$i.log || :
    done
    rm -f $files
    rm -rf $sockdir
}
cleanup_fn cleanup

# Run parallel nbdkit; to compare the logs and see what changes.
# 1: fuamode=none (default): client should send flush instead
# 2: fuamode=emulate: log shows that blocksize optimizes fua to flush
# 3: fuamode=native: log shows that blocksize preserves fua
# 4: fuamode=force: log shows that fua is always enabled
# 5: fuamode=pass: fua flag and flush unchanged
# 6: fuamode=discard: discard all fua and flush
start_nbdkit -P fua1.pid -U $sockdir/fua1.sock \
             --filter=log --filter=fua \
             file logfile=fua1.log fua.img
start_nbdkit -P fua2.pid -U $sockdir/fua2.sock \
             --filter=blocksize --filter=log --filter=fua \
             file logfile=fua2.log fua.img fuamode=emulate maxdata=4k maxlen=4k
start_nbdkit -P fua3.pid -U $sockdir/fua3.sock \
             --filter=blocksize --filter=log --filter=fua \
             file logfile=fua3.log fua.img fuamode=native maxdata=4k maxlen=4k
start_nbdkit -P fua4.pid -U $sockdir/fua4.sock \
             --filter=fua --filter=log \
             file logfile=fua4.log fua.img fuamode=force
start_nbdkit -P fua5.pid -U $sockdir/fua5.sock \
             --filter=fua --filter=log \
             file logfile=fua5.log fua.img fuamode=pass
start_nbdkit -P fua6.pid -U $sockdir/fua6.sock \
             --filter=fua --filter=log \
             file logfile=fua6.log fua.img fuamode=discard

# Perform a flush, write, and zero, first without then with FUA
for f in '' -f; do
    for i in {1..6}; do
	qemu-io -f raw -t none -c flush -c "w $f 0 64k" -c "w -z $f 64k 64k" \
		 "nbd+unix://?socket=$sockdir/fua$i.sock"
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
fua=$(grep -c 'connection=2.*fua=1 .*\.' fua2.log || :)
test $(( $flush2 - $flush1 + $fua )) = 2

# Test 3: every part of split has fua, and no flushes are added
flush1=$(grep -c 'connection=1 Flush' fua3.log || :)
flush2=$(grep -c 'connection=2 Flush' fua3.log || :)
test $flush1 = $flush2
test $(grep -c 'connection=2.*fua=1 .*\.' fua3.log) = 32

# Test 4: flush is no-op, and every transaction has fua
if grep 'fua=0' fua4.log; then
    echo "filter should have forced fua"
    exit 1
fi
if grep 'Flush' fua4.log; then
    echo "filter should have elided flush"
    exit 1
fi

# Test 5: Flush should be passed through.
# There should also be one set of fua=0 and a second set of fua=1.
grep 'Flush' fua5.log
grep 'connection=1 Write.*fua=0' fua5.log
grep 'connection=2 Write.*fua=1' fua5.log
grep 'connection=1 Zero.*fua=0' fua5.log
grep 'connection=2 Zero.*fua=1' fua5.log

# Test 6: Flush and fua=1 must not appear.
if grep 'Flush' fua6.log; then
    echo "filter should have elided flush"
    exit 1
fi
if grep -E '(Write|Zero).*fua=1' fua6.log; then
    echo "filter should have elided fua"
    exit 1
fi
