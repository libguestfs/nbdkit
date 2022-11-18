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

# Test --vsock option.
#
# This test is only possible on Linux >= 5.6 because that is the first
# version supporting loopback AF_VSOCK (so you can run client and
# server on the same host without needing to use a virtual machine).
#
# It also requires libnbd with nbdinfo, URI and vsock support.

source ./functions.sh
set -e
set -x

requires_nbdinfo
requires nbdsh --version
requires nbdsh -c 'print(h.connect_vsock)'
requires_nbdsh_uri
requires_linux_kernel_version 5.6
requires_vsock_support

# Because vsock ports are 32 bits, we can basically pick one at random
# and be sure that it's not used.  However we must pick one >= 1024
# because the ports below this are privileged.
#port=$(( 1024 + $RANDOM + ($RANDOM << 16) ))
#
# We would do that, but libxml2 is broken, see:
# https://mail.gnome.org/archives/xml/2020-October/msg00001.html
# https://mail.gnome.org/archives/xml/2020-October/msg00002.html
port=$(( 1024 + $RANDOM + ($RANDOM << 11) ))

nbdkit --vsock --port $port -v null 1M --run 'nbdinfo "$uri"'
