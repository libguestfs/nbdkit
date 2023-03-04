#!/usr/bin/env bash
# nbdkit
# Copyright Red Hat
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

requires_filter cow
requires_plugin file
requires test -f disk
requires cmp --version
requires dd --version
requires guestfish --version
requires_nbdcopy

copy=cow-extents2.img
sparse=cow-extents2-sparse.img
pid=cow-extents2.pid
sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="$copy $sparse $pid $sock"
rm -f $files
cleanup_fn rm -f $files

# Copy the original disk using 'dd' so it becomes fully allocated.
dd if=disk of=$copy

# Run nbdkit with the cow filter on top.
start_nbdkit -P $pid -U $sock --filter=cow file $copy

guestfish -a "nbd:///?socket=$sock" <<EOF
  run
# Sparsify it into the overlay.
  mount /dev/sda1 /
  fstrim /
  umount /dev/sda1
# After remounting check we can list the files and read the hello.txt file.
  mount /dev/sda1 /
  find /
  cat /hello.txt
EOF

# Copy out the disk to a local file.
# This should exercise extents information.
nbdcopy "nbd+unix:///?socket=$sock" $sparse

# We can only visually check that the file has been sparsified.  We do
# not expect the files to be bit for bit identical after running
# fstrim.
ls -lsh disk $copy $sparse
