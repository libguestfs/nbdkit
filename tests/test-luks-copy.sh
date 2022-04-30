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

source ./functions.sh
set -e
set -x

requires nbdcopy --version
requires nbdsh --version
requires_nbdsh_uri
requires qemu-img --version
requires bash -c 'qemu-img --help | grep -- --target-image-opts'
requires hexdump --version
requires truncate --version
requires_filter luks

encrypt_disk=luks-copy1.img
plain_disk=luks-copy2.img
pid=luks-copy.pid
sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
cleanup_fn rm -f $encrypt_disk $plain_disk $pid $sock
rm -f $encrypt_disk $plain_disk $pid $sock

# Create an empty encrypted disk container.
#
# NB: This is complicated because qemu doesn't create an all-zeroes
# plaintext disk for some reason when you use create -f luks.  It
# starts with random plaintext.
#
# https://stackoverflow.com/a/44669936
qemu-img create -f luks \
         --object secret,data=123456,id=sec0 \
         -o key-secret=sec0 \
         $encrypt_disk 10M
truncate -s 10M $plain_disk
qemu-img convert --target-image-opts -n \
         --object secret,data=123456,id=sec0 \
         $plain_disk \
         driver=luks,file.filename=$encrypt_disk,key-secret=sec0
rm $plain_disk

# Start nbdkit on the encrypted disk.
start_nbdkit -P $pid -U $sock \
             file $encrypt_disk --filter=luks passphrase=123456
uri="nbd+unix:///?socket=$sock"

# Copy the whole disk out.  It should be empty.
nbdcopy "$uri" $plain_disk

if [ "$(hexdump -C $plain_disk)" != '00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00a00000' ]; then
    echo "$0: expected plaintext disk to be empty"
    exit 1
fi

# Use nbdsh to overwrite with some known data and check we can read
# back what we wrote.
nbdsh -u "$uri" \
      -c 'h.pwrite(b"1"*65536, 0)' \
      -c 'h.pwrite(b"2"*65536, 128*1024)' \
      -c 'h.pwrite(b"3"*65536, 9*1024*1024)' \
      -c 'buf = h.pread(65536, 0)' \
      -c 'assert buf == b"1"*65536' \
      -c 'buf = h.pread(65536, 65536)' \
      -c 'assert buf == bytearray(65536)' \
      -c 'buf = h.pread(65536, 128*1024)' \
      -c 'assert buf == b"2"*65536' \
      -c 'buf = h.pread(65536, 9*1024*1024)' \
      -c 'assert buf == b"3"*65536' \
      -c 'h.flush()'

# Use qemu to copy out the whole disk.  Note we called flush() above
# so the disk should be synchronised.
qemu-img convert --image-opts \
         --object secret,data=123456,id=sec0 \
         driver=luks,file.filename=$encrypt_disk,key-secret=sec0 \
         $plain_disk

# Check the contents are expected.
if [ "$(hexdump -C $plain_disk)" != '00000000  31 31 31 31 31 31 31 31  31 31 31 31 31 31 31 31  |1111111111111111|
*
00010000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00020000  32 32 32 32 32 32 32 32  32 32 32 32 32 32 32 32  |2222222222222222|
*
00030000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00900000  33 33 33 33 33 33 33 33  33 33 33 33 33 33 33 33  |3333333333333333|
*
00910000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00a00000' ]; then
    echo "$0: unexpected content"
    exit 1
fi
