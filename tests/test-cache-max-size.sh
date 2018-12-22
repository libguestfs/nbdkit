#!/usr/bin/env bash
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

source ./functions.sh
set -e
set -x

# Check that this is a Linux-like system supporting /proc/$pid/fd.
if ! test -d /proc/self/fd; then
    echo "$0: not a Linux-like system supporting /proc/\$pid/fd"
    exit 77
fi

# Test that qemu-io works
if ! qemu-io --help >/dev/null; then
    echo "$0: missing or broken qemu-io"
    exit 77
fi

# Need the stat command from coreutils.
if ! stat --version >/dev/null; then
    echo "$0: missing or broken stat command"
    exit 77
fi

d=cache-max-size.d
rm -rf $d
mkdir -p $d
cleanup_fn rm -rf $d

# Create a cache directory.
mkdir $d/cache
TMPDIR=$d/cache
export TMPDIR

# Create an empty base image.
truncate -s 1G $d/cache-max-size.img

# Run nbdkit with the caching filter and a low size limit to ensure
# that the reclaim code is exercised.
start_nbdkit -P $d/cache-max-size.pid -U $d/cache-max-size.sock \
             --filter=cache \
             file $d/cache-max-size.img \
             cache-max-size=10M cache-on-read=true

# Write > 10M to the plugin.
qemu-io -f raw "nbd+unix://?socket=$d/cache-max-size.sock" \
        -c "w -P 0 0 10M" \
        -c "w -P 0 20M 10M" \
        -c "r 20M 10M" \
        -c "r 30M 10M" \
        -c "w -P 0 40M 10M"

# We can't use ‘du’ on the cache directory because the cache file is
# deleted by the filter, and so is only accessible via /proc/$pid/fd.
# Get the /proc link to the cache file, and the size of it in bytes.
fddir="/proc/$( cat $d/cache-max-size.pid )/fd"
ls -l $fddir
fd="$fddir/$( ls -l $fddir | grep $TMPDIR | head -1 | awk '{print $9}' )"
stat -L $fd
size=$(( $(stat -L -c '%b * %B' $fd) ))

if [ "$size" -gt $(( 11 * 1024 * 1024 )) ]; then
    echo "$0: cache size is larger than 10M (actual size: $size bytes)"
    exit 1
fi
