#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2017 Red Hat Inc.
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

requires_daemonizing
requires qemu-img --version

if ! qemu-img --help | grep -- --object; then
    echo "$0: 'qemu-img' command does not have the --object option"
    exit 77
fi

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

# Unfortunately qemu 4.0 cannot do TLS over a Unix domain socket (nbdkit
# can, but that is tested in tests-nbd-tls.sh).  Find an unused port to
# listen on.
pick_unused_port

cleanup_fn rm -f tls.pid tls.out
start_nbdkit -P tls.pid -p $port -n --tls=require \
       --tls-certificates="$pkidir" example1

# Run qemu-img against the server.
qemu-img info --output=json \
         --object "tls-creds-x509,id=tls0,endpoint=client,dir=$pkidir" \
         --image-opts "file.driver=nbd,file.host=localhost,file.port=$port,file.tls-creds=tls0" > tls.out

cat tls.out

grep -sq '"format": *"raw"' tls.out
grep -sq '"virtual-size": *104857600\b' tls.out
