#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2019-2020 Red Hat Inc.
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

requires qemu-img --version

# Does the nbdkit binary support TLS?
if ! nbdkit --dump-config | grep -sq tls=yes; then
    echo "$0: nbdkit built without TLS support"
    exit 77
fi

# Does the nbd plugin support TLS?
if ! nbdkit --dump-plugin nbd | grep -sq libnbd_tls=1; then
    echo "$0: nbd plugin built without TLS support"
    exit 77
fi

# Did we create the PSK keys file?
# Probably 'certtool' is missing.
if [ ! -s keys.psk ]; then
    echo "$0: PSK keys file was not created by the test harness"
    exit 77
fi

sock1=`mktemp -u`
sock2=`mktemp -u`
pid1="test-nbd-tls-psk.pid1"
pid2="test-nbd-tls-psk.pid2"

files="$sock1 $sock2 $pid1 $pid2 nbd-tls-psk.out"
rm -f $files
cleanup_fn rm -f $files

# Run nbd plugin as intermediary; also test our retry code.  We start this
# instance of nbdkit first because the two nbdkit processes will be killed
# in the same order; and it is easier to kill the nbd client (which is
# poll()ing on a non-blocking socket) than the example1 server (which is
# read()ing on a blocking socket) if both sides are waiting for the other
# to perform gnutls_bye() before closing the socket.
start_nbdkit -P "$pid2" -U "$sock2" --tls=off nbd retry=10 \
    tls=require tls-psk=keys.psk tls-username=qemu socket="$sock1"

# Run unencrypted client in background, so that retry will be required
qemu-img info --output=json -f raw "nbd+unix:///?socket=$sock2" \
	 > nbd-tls-psk.out &
info_pid=$!
sleep 1

# Run encrypted server
start_nbdkit -P "$pid1" -U "$sock1" \
    --tls=require --tls-psk=keys.psk -D nbdkit.gnutls.session=1 example1

wait $info_pid
cat nbd-tls-psk.out

grep -sq '"format": *"raw"' nbd-tls-psk.out
grep -sq '"virtual-size": *104857600\b' nbd-tls-psk.out
