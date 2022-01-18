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

requires test "x$vddkdir" != "x"
requires test -d "$vddkdir"
requires test -f "$vddkdir/lib64/libvixDiskLib.so"
requires test -f disk
requires nbdcopy --version
requires stat --version

# Testing $LD_LIBRARY_PATH stuff breaks valgrind, so skip the rest of
# this test if valgrinding.
if [ "x$NBDKIT_VALGRIND" = "x1" ]; then
    echo "$0: skipped LD_LIBRARY_PATH test when doing valgrind"
    exit 77
fi

# VDDK > 5.1.1 only supports x86_64.
if [ `uname -m` != "x86_64" ]; then
    echo "$0: unsupported architecture"
    exit 77
fi

vmdk=$PWD/test-vddk-real-create.vmdk ;# note must be an absolute path
files="$vmdk"
rm -f $files
cleanup_fn rm -f $files

size="$(stat -c %s disk)"

nbdkit -fv -U - vddk libdir="$vddkdir" $vmdk \
       create=true create-size=$size \
       --run 'nbdcopy disk $uri'

# Check the VMDK file was created and looks reasonable.
test -f $vmdk
file $vmdk | grep 'VMware'
