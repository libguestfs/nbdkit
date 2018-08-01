#!/bin/bash -
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

files="cache.img cache.sock cache.pid"
rm -f $files

# Create an empty base image.
truncate -s 1G cache.img

# Run nbdkit with the caching filter.
nbdkit -P cache.pid -U cache.sock --filter=cache file file=cache.img

# We may have to wait a short time for the pid file to appear.
for i in `seq 1 10`; do
    if test -f cache.pid; then
        break
    fi
    sleep 1
done
if ! test -f cache.pid; then
    echo "$0: PID file was not created"
    exit 1
fi

pid="$(cat cache.pid)"

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

# Open the overlay and perform some operations.
guestfish --format=raw -a "nbd://?socket=$PWD/cache.sock" <<'EOF'
  run
  part-disk /dev/sda gpt
  mkfs ext4 /dev/sda1
  mount /dev/sda1 /
  fill-dir / 10000
  fill-pattern "abcde" 5M /large
  write /hello "hello, world"
EOF

# Check the last files we created exist.
guestfish --ro -a cache.img -m /dev/sda1 <<'EOF'
  cat /hello
  cat /large | cat >/dev/null
EOF

# The cleanup() function is called implicitly on exit.
