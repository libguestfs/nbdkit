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

# Regression test for:
# https://listman.redhat.com/archives/libguestfs/2022-June/029188.html

source ./functions.sh
set -e
set -x

requires qemu-img --version
requires nbdcopy --version
requires truncate --version
requires file --version
requires_filter luks

encrypt_disk=luks-copy-zero1.img
zero_disk=luks-copy-zero2.img
cleanup_fn rm -f $encrypt_disk $zero_disk
rm -f $encrypt_disk $zero_disk

# Create an empty encrypted disk container.
qemu-img create -f luks \
         --object secret,data=123456,id=sec0 \
         -o key-secret=sec0 \
         $encrypt_disk 100M

# Create an all zeroes disk of the same size.
truncate -s 100M $zero_disk

# Using nbdkit-luks-filter, write the zero disk into the encrypted
# disk.  nbdcopy will do this using NBD_CMD_ZERO operations.
nbdkit -U - -fv \
       file $encrypt_disk --filter=luks passphrase=123456 \
       --run "nbdcopy -C 1 $zero_disk \$nbd"

# Check that the encrypted disk is still a LUKS disk.  If zeroing is
# wrong in the filter it's possible that it writes through to the
# underlying disk, erasing the container.
file $encrypt_disk
file $encrypt_disk | grep "LUKS encrypted file"
