#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2016-2020 Red Hat Inc.
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
# IPv4 and IPv6 localhost connections.

source ./functions.sh
set -e

requires_daemonizing
requires ip -V
requires qemu-img --version
requires qemu-img info --image-opts driver=file,filename=functions.sh
requires_ipv6_loopback

rm -f ip.pid ipv4.out ipv6.out
cleanup_fn rm -f ip.pid ipv4.out ipv6.out

# Find an unused port to listen on.
pick_unused_port

# By default nbdkit will listen on all available interfaces, ie.
# IPv4 and IPv6.
start_nbdkit -P ip.pid -p $port example1
pid="$(cat ip.pid)"

# Check the process exists.
kill -s 0 $pid

# Check we can connect over the IPv4 loopback interface.
ipv4_lo="$(ip -o -4 addr show scope host)"
if test -n "$ipv4_lo"; then
    qemu-img info --output=json \
        --image-opts "file.driver=nbd,file.host=127.0.0.1,file.port=$port" > ipv4.out
    cat ipv4.out
    grep -sq '"virtual-size": *104857600\b' ipv4.out
fi

# Check we can connect over the IPv6 loopback interface.
ipv6_lo="$(ip -o -6 addr show scope host)"
if test -n "$ipv6_lo"; then
    qemu-img info --output=json \
        --image-opts "file.driver=nbd,file.host=::1,file.port=$port" > ipv6.out
    cat ipv6.out
    grep -sq '"virtual-size": *104857600\b' ipv6.out
fi
