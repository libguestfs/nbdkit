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

sock=`mktemp -u`
files="cache.img $sock cache.pid"
rm -f $files
cleanup_fn rm -f $files

# Create an empty base image.
truncate -s 1G cache.img

# Run nbdkit with the caching filter.
start_nbdkit -P cache.pid -U $sock --filter=cache file cache.img

# Open the overlay and perform some operations.
guestfish --format=raw -a "nbd://?socket=$sock" <<'EOF'
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
