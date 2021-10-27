#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018-2020 Red Hat Inc.
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
requires test -r /dev/urandom
requires cmp --version
requires dd --version
requires qemu-img --version
requires nbdcopy --version
requires nbdinfo --version
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

# Since we are comparing error messages below, let's make sure we're
# not translating errors.
export LANG=C

# Strict malloc checking breaks VDDK 7.0.0 and 7.0.3.
unset GLIBC_TUNABLES

pid=test-vddk-real.pid
sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
vmdk=$PWD/test-vddk-real.vmdk ;# note must be an absolute path
raw=test-vddk-real.raw
raw2=test-vddk-real.raw2
log=test-vddk-real.log
files="$pid $sock $vmdk $raw $raw2 $log"
rm -f $files
cleanup_fn rm -f $files

qemu-img create -f vmdk $vmdk 10M

# Check first that the VDDK library can be fully loaded.  We have to
# check the log file for missing modules since they may not show up as
# errors.
nbdkit -fv -U - vddk libdir="$vddkdir" $vmdk --run 'nbdinfo "$uri"' 2>&1 |
    tee $log

# Check the log for missing modules
if grep 'cannot open shared object file' $log; then
   exit 1
fi

# Now run nbdkit for the test.
start_nbdkit -P $pid -U $sock -D vddk.stats=1 -D vddk.diskinfo=1 \
             vddk libdir="$vddkdir" $vmdk
uri="nbd+unix:///?socket=$sock"

# VDDK < 6.0 did not support flush, so disable flush test there.  Also
# if nbdinfo doesn't support the --can flush syntax (added in libnbd
# 1.10) then this is disabled.
if nbdinfo --can flush "$uri"; then flush="--flush"; else flush=""; fi

# Copy in and out some data.  This should exercise read, write,
# extents and flushing.
dd if=/dev/urandom of=$raw count=5 bs=$((1024*1024))
truncate -s 10M $raw

nbdcopy $flush $raw "$uri"
nbdcopy "$uri" $raw2

cmp $raw $raw2
