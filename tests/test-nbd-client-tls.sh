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

# Check that nbd-client (the kernel client) can interoperate with
# nbdkit with TLS.

source ./functions.sh
set -e
set -x

nbddev=/dev/nbd2

requires_root

requires nbd-client --version
requires_not nbd-client -c $nbddev

requires blockdev --version
requires dd --version
requires hexdump --version

# NBD support was added in 2.1.55!  Mainly we're using this to check
# this is Linux.
requires_linux_kernel_version 2.2

# Does the nbdkit binary support TLS?
if ! nbdkit --dump-config | grep -sq tls=yes; then
    echo "$0: nbdkit built without TLS support"
    exit 77
fi

# Did we create the PKI files?
# Probably 'certtool' is missing.
pkidir="$PWD/pki"
if [ ! -f "$pkidir/ca-cert.pem" ]; then
    echo "$0: PKI files were not created by the test harness"
    exit 77
fi

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
pid=nbd-client-tls.pid
rm -f $sock $pid
cleanup_fn rm -f $sock $pid

# Try to make sure the nbd device is cleaned up on exit.
#
# We have to run this here so we run this command first, before trying
# to kill nbdkit.  (The order in which the cleanup hooks run should
# probably be reversed).
cleanup_fn nbd-client -d $nbddev

# Start an nbdkit instance serving known data and allowing writes.
start_nbdkit -P $pid -U $sock \
             --tls=require --tls-certificates="$pkidir" --tls-verify-peer \
             pattern 10M --filter=cow

# Open a connection with nbd-client.
nbd-client -unix $sock $nbddev \
           -cacertfile $pkidir/ca-cert.pem \
           -certfile $pkidir/client-cert.pem \
           -keyfile $pkidir/client-key.pem

# Check the device exists.
nbd-client -c $nbddev
size="$( blockdev --getsize64 $nbddev )"
test "$size" -eq $(( 10 * 1024 * 1024 ))

# Check the data in the device looks reasonable.
dd if=$nbddev bs=1024 count=1 skip=1 | hexdump -C

# Try writing.
dd if=/dev/zero of=$nbddev bs=1024 count=100 skip=200
