#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2019-2023 Red Hat Inc.
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
set -x

requires nbdsh -c 'exit(not h.supports_tls())'


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

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="client-death-tls.pid $sock"
cleanup_fn rm -f $files

# Start long-running nbdkit
start_nbdkit -P client-death-tls.pid --tls require --tls-psk=keys.psk \
             -U $sock memory 2M

pid=`cat client-death-tls.pid`

# We can't use 'nbdsh -u "$uri" because of nbd_set_uri_allow_local_file.
# Run a client that abandons several in-flight requests, each large enough
# that we should see EPIPE on one handler while other handlers are still
# waiting to send their response.
nbdsh -c '
h.set_tls(nbd.TLS_REQUIRE)
h.set_tls_psk_file("keys.psk")
h.set_tls_username("qemu")
h.connect_unix("'"$sock"'")

buf = nbd.Buffer(2*1024*1024)
c1 = h.aio_pread(buf, 0)
c2 = h.aio_pread(buf, 0)
c3 = h.aio_pread(buf, 0)
c4 = h.aio_pread(buf, 0)
'

# nbdkit should still be running
sleep 1
kill -s 0 $pid
