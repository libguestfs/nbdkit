#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2020 Red Hat Inc.
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

requires qemu-img --version
requires qemu-img compare --help
requires stat --version
requires test -f disk

out=test-streaming-qemu-read.qcow2
pipe=test-streaming-qemu-read.pipe
rm -f $out $pipe
cleanup_fn rm -f $out $pipe

# We need to specify the size of the input else qemu-img convert
# complains.  XXX why?
size="$( stat -c '%s' disk )"

nbdkit --exit-with-parent -U - streaming read=$pipe size=$size \
       --run " qemu-img convert -f raw \$nbd -O qcow2 $out " &
pid=$!
cleanup_fn kill $pid

# Wait for the plugin to create the pipe.
for i in {1..60}; do
    if test -p $pipe; then
        break
    fi
    sleep 1
done
if ! test -p $pipe; then
    echo "$0: nbdkit did not create the pipe"
    exit 1
fi

cat disk > $pipe

wait $pid

# Compare the original disk and the output (qcow2 format).  This works
# even though the formats are not the same.
qemu-img compare -f raw disk -F qcow2 $out
