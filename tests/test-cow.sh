#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018 Red Hat Inc.
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

requires_unix_domain_sockets
requires guestfish --version

sock=`mktemp -u`
files="cow-base.img cow-diff.qcow2 $sock cow.pid"
rm -f $files
cleanup_fn rm -f $files

# Create a base image which is partitioned with an empty filesystem.
rm -rf cow.d
mkdir cow.d
cleanup_fn rm -rf cow.d
nbdkit -fv -U - linuxdisk cow.d size=100M \
       --run 'qemu-img convert $nbd cow-base.img'
lastmod="$(stat -c "%y" cow-base.img)"

# Run nbdkit with a COW overlay.
start_nbdkit -P cow.pid -U $sock --filter=cow file cow-base.img

# Write some data into the overlay.
guestfish --format=raw -a "nbd://?socket=$sock" -m /dev/sda1 <<EOF
  fill-pattern "abcde" 128K /large
  write /hello "hello, world"
EOF

# The original file must not be modified.
currmod="$(stat -c "%y" cow-base.img)"

if [ "$lastmod" != "$currmod" ]; then
    echo "$0: FAILED last modified time of base file changed"
    exit 1
fi

# If we have qemu-img, try the hairy rebase operation documented
# in the nbdkit-cow-filter manual.
if qemu-img --version >/dev/null 2>&1; then
    qemu-img create -f qcow2 -b nbd:unix:$sock cow-diff.qcow2
    time qemu-img rebase -b cow-base.img cow-diff.qcow2
    qemu-img info cow-diff.qcow2

    # This checks the file we created exists.
    guestfish --ro -a cow-diff.qcow2 -m /dev/sda1 cat /hello
fi
