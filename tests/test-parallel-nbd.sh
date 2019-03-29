#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2017-2018 Red Hat Inc.
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

# Check file-data was created by Makefile and qemu-io exists.
requires test -f file-data
requires qemu-io --version

# We require --exit-with-parent to work
( nbdkit --exit-with-parent --help ) >/dev/null 2>&1 ||
  { echo "Missing --exit-with-parent support"; exit 77; }

files="test-parallel-nbd.out test-parallel-nbd.sock test-parallel-nbd.data test-parallel-nbd.pid"
rm -f $files
cleanup_fn rm -f $files

# Populate file, and sanity check that qemu-io can issue parallel requests
printf '%1024s' . > test-parallel-nbd.data
qemu-io -f raw -c "aio_write -P 1 0 512" -c "aio_write -P 2 512 512" \
         -c aio_flush test-parallel-nbd.data ||
    { echo "'qemu-io' can't drive parallel requests"; exit 77; }

# Set up the file plugin to delay both reads and writes (for a good chance
# that parallel requests are in flight), and with writes longer than reads
# (to more easily detect if out-of-order completion happens).  This test
# may have spurious failures under heavy loads on the test machine, where
# tuning the delays may help.

start_nbdkit -P test-parallel-nbd.pid \
             -U test-parallel-nbd.sock \
             --filter=delay \
             file test-parallel-nbd.data wdelay=2 rdelay=1

# With --threads=1, the write should complete first because it was issued first
nbdkit -v -t 1 -U - nbd socket=test-parallel-nbd.sock --run '
  qemu-io -f raw -c "aio_write -P 2 512 512" -c "aio_read -P 1 0 512" \
  -c aio_flush $nbd' | tee test-parallel-nbd.out
if test "$(grep '512/512' test-parallel-nbd.out)" != \
"wrote 512/512 bytes at offset 512
read 512/512 bytes at offset 0"; then
  exit 1
fi

# With default --threads, the faster read should complete first
nbdkit -v -U - nbd socket=test-parallel-nbd.sock --run '
  qemu-io -f raw -c "aio_write -P 2 512 512" -c "aio_read -P 1 0 512" \
  -c aio_flush $nbd' | tee test-parallel-nbd.out
if test "$(grep '512/512' test-parallel-nbd.out)" != \
"read 512/512 bytes at offset 0
wrote 512/512 bytes at offset 512"; then
  exit 1
fi
