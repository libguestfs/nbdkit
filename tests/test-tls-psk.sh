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
set -x
source ./functions.sh

# Don't fail if certain commands aren't available.
if ! ss --version; then
    echo "$0: 'ss' command not available"
    exit 77
fi
if ! command -v qemu-img > /dev/null; then
    echo "$0: 'qemu-img' command not available"
    exit 77
fi
if ! qemu-img --help | grep -- --object; then
    echo "$0: 'qemu-img' command does not have the --object option"
    exit 77
fi

# Does the qemu-img binary support PSK?
if LANG=C qemu-img info --object tls-creds-psk,id=id disk |&
        grep -sq "invalid object type: tls-creds-psk"
then
    echo "$0: 'qemu-img' command does not support TLS-PSK"
    exit 77
fi

# Does the nbdkit binary support TLS?
if ! nbdkit --dump-config | grep -sq tls=yes; then
    echo "$0: nbdkit built without TLS support"
    exit 77
fi

# Did we create the PSK keys file?
# Probably 'certtool' is missing.
if [ ! -s keys.psk ]; then
    echo "$0: PSK keys file was not created by the test harness"
    exit 77
fi

# Unfortunately qemu cannot do TLS over a Unix domain socket (nbdkit
# probably can, although it is not tested).  Find an unused port to
# listen on.
for port in `seq 50000 65535`; do
    if ! ss -ltn | grep -sqE ":$port\b"; then break; fi
done
echo picked unused port $port

nbdkit -P tls-psk.pid -p $port -n --tls=require --tls-psk=keys.psk example1

# We may have to wait a short time for the pid file to appear.
for i in `seq 1 10`; do
    if test -f tls-psk.pid; then
        break
    fi
    sleep 1
done
if ! test -f tls-psk.pid; then
    echo "$0: PID file was not created"
    exit 1
fi

pid="$(cat tls-psk.pid)"

# Kill the process on exit.
cleanup ()
{
    status=$?
    trap '' INT QUIT TERM EXIT ERR
    echo $0: cleanup: exit code $status

    kill $pid
    rm -f tls-psk.pid tls-psk.out

    exit $status
}
trap cleanup INT QUIT TERM EXIT ERR

# Run qemu-img against the server.
LANG=C \
qemu-img info \
         --object "tls-creds-psk,id=tls0,endpoint=client,dir=$PWD" \
         --image-opts "file.driver=nbd,file.host=localhost,file.port=$port,file.tls-creds=tls0" > tls-psk.out

cat tls-psk.out

grep -sq "^file format: raw" tls-psk.out
grep -sq "^virtual size: 100M" tls-psk.out
