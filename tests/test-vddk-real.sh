#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018-2019 Red Hat Inc.
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

requires test "x$vddkdir" != "x"
requires test -d "$vddkdir"
requires test -f "$vddkdir/lib64/libvixDiskLib.so"
requires qemu-img --version

# VMware only supports i686 or x86_64 so this is fine:
case `uname -m` in
    i?86) lib=lib32 ;;
    x86_64) lib=lib64 ;;
    *)
        echo "$0: unsupported architecture"
        exit 77
esac

files="test-vddk-real.vmdk test-vddk-real.out"
rm -f $files
cleanup_fn rm -f $files

qemu-img create -f vmdk test-vddk-real.vmdk 100M

export old_ld_library_path="$LD_LIBRARY_PATH"
export LD_LIBRARY_PATH="$vddkdir/$lib:$LD_LIBRARY_PATH"

nbdkit -f -v -U - \
       --filter=readahead \
       vddk libdir="$vddkdir" file=test-vddk-real.vmdk \
       --run '
       # VDDK library path breaks qemu-img, we must restore the
       # original path here.
       export LD_LIBRARY_PATH="$old_ld_library_path"
       qemu-img convert $nbd -O raw test-vddk-real.out
'
