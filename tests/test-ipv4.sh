#!/bin/bash -
# nbdkit
# Copyright (C) 2016 Red Hat Inc.
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

# Every other test uses a Unix domain socket.  This tests nbdkit over
# IPv4 localhost connections.
#
# XXX We should be able to test "just IPv6".  However there is
# currently no option to listen only on particular interfaces.

set -e
source ./functions.sh

# Don't fail if certain commands aren't available.
if ! ss --version; then
    echo "$0: 'ss' command not available"
    exit 77
fi
if ! socat -h; then
    echo "$0: 'socat' command not available"
    exit 77
fi

# Find an unused port to listen on.
for port in `seq 49152 65535`; do
    if ! ss -ltn | grep -sqE ":$port\b"; then break; fi
done
echo picked unused port $port

nbdkit -P ipv4.pid -p $port example1

# We may have to wait a short time for the pid file to appear.
for i in `seq 1 10`; do
    if test -f ipv4.pid; then
        break
    fi
    sleep 1
done
if ! test -f ipv4.pid; then
    echo "$0: PID file was not created"
    exit 1
fi

pid="$(cat ipv4.pid)"

# Check the process exists.
kill -s 0 $pid

# Check we can connect to the socket.
socat TCP:localhost:$port STDIO </dev/null

# Kill the process.
kill $pid
rm ipv4.pid
