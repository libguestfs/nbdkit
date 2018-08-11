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

files="cow-base.img cow-diff.qcow2 cow.sock cow.pid"
rm -f $files

# Create a base image which is partitioned with an empty filesystem.
guestfish -N cow-base.img=fs exit
lastmod="$(stat -c "%y" cow-base.img)"

# Run nbdkit with a COW overlay.
nbdkit -P cow.pid -U cow.sock --filter=cow file file=cow-base.img

# We may have to wait a short time for the pid file to appear.
for i in `seq 1 10`; do
    if test -f cow.pid; then
        break
    fi
    sleep 1
done
if ! test -f cow.pid; then
    echo "$0: PID file was not created"
    exit 1
fi

pid="$(cat cow.pid)"

# Kill the nbdkit process on exit.
cleanup ()
{
    status=$?
    trap '' INT QUIT TERM EXIT ERR
    echo $0: cleanup: exit code $status

    kill $pid
    rm -f $files

    exit $status
}
trap cleanup INT QUIT TERM EXIT ERR

# Write some data into the overlay.
guestfish --format=raw -a "nbd://?socket=$PWD/cow.sock" -m /dev/sda1 <<EOF
  fill-dir / 10000
  fill-pattern "abcde" 5M /large
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
    qemu-img create -f qcow2 -b nbd:unix:cow.sock cow-diff.qcow2
    time qemu-img rebase -b cow-base.img cow-diff.qcow2
    qemu-img info cow-diff.qcow2

    # This checks the file we created exists.
    guestfish --ro -a cow-diff.qcow2 -m /dev/sda1 cat /hello
fi

# The cleanup() function is called implicitly on exit.
