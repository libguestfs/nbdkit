#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018-2022 Red Hat Inc.
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

# Demonstrate a fix for a bug where blocksize could lose aligned writes
# run in parallel with unaligned writes

source ./functions.sh
set -e
set -x

requires_plugin eval
requires_nbdsh_uri

files='blocksize-sharding.img blocksize-sharding.tmp'
rm -f $files
cleanup_fn rm -f $files

# Script a server that requires 16-byte aligned requests, and which delays
# 4s after reads if a witness file exists.  Couple it with the delay filter
# that delays 2s before writes. If an unaligned and aligned write overlap,
# and can execute in parallel, we would have this timeline:
#
#     T1 aligned write 1's to 0/16     T2 unaligned write 2's to 4/12
#t=0  blocksize: next->pwrite(0, 16)   blocksize: next->pread(0, 16)
#       delay: wait 2s                   delay: next->pread(0, 16)
#       ...                                eval: read 0's, wait 4s
#t=2    delay: next->pwrite(0, 16)         ...
#         eval: write 1's                  ...
#     return                               ...
#t=4                                     return 0's (now stale)
#                                      blocksize: next->pwrite(0, 16)
#                                        delay: wait 2s
#t=6                                     delay: next->pwrite(0, 16)
#                                          eval: write stale RMW buffer
#
# leaving us with a sharded 0000222222222222 (T1's write is lost).
# But as long as the blocksize filter detects the overlap, we should end
# up with either 1111222222222222 (aligned write completed first), or with
# 1111111111111111 (unaligned write completed first), either taking 8s,
# but with no sharding.
#
# We also need an nbdsh script that kicks off parallel writes.
export script='
import os
import time

witness = os.getenv("witness")

def touch(path):
    open(path, "a").close()

# First pass: check that two aligned operations work in parallel
# Total time should be closer to 2 seconds, rather than 4 if serialized
print("sanity check")
ba1 = bytearray(b"1"*16)
ba2 = bytearray(b"2"*16)
buf1 = nbd.Buffer.from_bytearray(ba1)
buf2 = nbd.Buffer.from_bytearray(ba2)
touch(witness)
start_t = time.time()
h.aio_pwrite(buf1, 0)
h.aio_pwrite(buf2, 0)

while h.aio_in_flight() > 0:
    h.poll(-1)
end_t = time.time()
os.unlink(witness)

out = h.pread(16,0)
print(out)
t = end_t - start_t
print(t)
assert out in [b"1"*16, b"2"*16]
assert t >= 2 and t <= 3

# Next pass: try to kick off unaligned first
print("unaligned first")
h.zero(16, 0)
ba3 = bytearray(b"3"*12)
ba4 = bytearray(b"4"*16)
buf3 = nbd.Buffer.from_bytearray(ba3)
buf4 = nbd.Buffer.from_bytearray(ba4)
touch(witness)
start_t = time.time()
h.aio_pwrite(buf3, 4)
h.aio_pwrite(buf4, 0)

while h.aio_in_flight() > 0:
    h.poll(-1)
end_t = time.time()
os.unlink(witness)

out = h.pread(16,0)
print(out)
t = end_t - start_t
print(t)
assert out in [b"4"*4 + b"3"*12, b"4"*16]
assert t >= 8

# Next pass: try to kick off aligned first
print("aligned first")
ba5 = bytearray(b"5"*16)
ba6 = bytearray(b"6"*12)
buf5 = nbd.Buffer.from_bytearray(ba5)
buf6 = nbd.Buffer.from_bytearray(ba6)
h.zero(16, 0)
touch(witness)
start_t = time.time()
h.aio_pwrite(buf5, 0)
h.aio_pwrite(buf6, 4)

while h.aio_in_flight() > 0:
    h.poll(-1)
end_t = time.time()
os.unlink(witness)

out = h.pread(16,0)
print(out)
t = end_t - start_t
print(t)
assert out in [b"5"*4 + b"6"*12, b"5"*16]
assert t >= 8
'

# Now run everything
$TRUNCATE -s 16 blocksize-sharding.img
export witness="$PWD/blocksize-sharding.tmp"
nbdkit -U - --filter=blocksize --filter=delay eval delay-write=2 \
    config='ln -sf "$(realpath "$3")" $tmpdir/$2' \
    img="$PWD/blocksize-sharding.img" tmp="$PWD/blocksize-sharding.tmp" \
    get_size='echo 16' block_size='echo 16 64K 1M' \
    thread_model='echo parallel' \
    zero='dd if=/dev/zero of=$tmpdir/img skip=$4 count=$3 \
      iflag=count_bytes,skip_bytes' \
    pread='
      dd if=$tmpdir/img skip=$4 count=$3 iflag=count_bytes,skip_bytes
      if [ -f $tmpdir/tmp ]; then sleep 4; fi ' \
    pwrite='dd of=$tmpdir/img seek=$4 conv=notrunc oflag=seek_bytes' \
    --run 'nbdsh -u "$uri" -c "$script"'
