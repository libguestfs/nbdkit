#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2017-2020 Red Hat Inc.
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

# Test that qemu-img info works on a qcow2 file in a tar.xz file.

source ./functions.sh
set -e
set -x

requires test -f disk
requires guestfish --version
requires tar --version
requires qemu-img --version
requires qemu-img info --output=json /dev/null
requires jq --version
requires $STAT --version
requires xz --version

disk=tar-info-disk-xz.qcow2
out=tar-info-xz.out
tar=tar-info-xz.tar
tar_xz=tar-info-xz.tar.xz
files="$disk $out $tar $tar_xz"
rm -f $files
cleanup_fn rm -f $files

# Create a tar file containing a known qcow2 file.
qemu-img convert -f raw disk -O qcow2 $disk
tar cf $tar $disk
xz --best --block-size=32768 $tar

# Run nbdkit.
nbdkit -U - file $tar_xz \
       --filter=tar tar-entry=$disk \
       --filter=xz \
       --run 'qemu-img info --output=json $nbd' > $out
cat $out

# Check various fields in the input.
# Virtual size must be the same as the size of the original raw disk.
test "$( jq -r -c '.["virtual-size"]' $out )" -eq "$( $STAT -c %s disk )"

# Format must be qcow2.
test "$( jq -r -c '.["format"]' $out )" = "qcow2"
