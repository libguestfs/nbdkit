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

# The output of qemu-img map on the local sparse file ‘disk’ should be
# identical to the output when we use the file plugin to read the
# extents.

source ./functions.sh
set -e
set -x

requires jq --version
requires qemu-img --version
requires qemu-img map --help

# The file plugin must support reading file extents.
requires sh -c 'nbdkit file --dump-plugin | grep file_extents=yes'

files="test-file-extents.tmp test-file-extents.nbdkit test-file-extents.local"
rm -f $files
cleanup_fn rm -f $files

# We can't guarantee whether disk will be sparse, or even what filesystem
# it is on (qemu might avoid SEEK_HOLE for performance reasons); add some
# debug crumbs, but don't fail the test if there is no GNU coreutils stat.
stat disk || :
stat -f disk || :

qemu-img map -f raw --output=json disk > test-file-extents.tmp
cat test-file-extents.tmp
jq -c '.[] | {start:.start, length:.length, data:.data, zero:.zero}' \
  < test-file-extents.tmp > test-file-extents.local
nbdkit -U - file disk --run 'qemu-img map -f raw --output=json $nbd' \
  > test-file-extents.tmp
cat test-file-extents.tmp
jq -c '.[] | {start:.start, length:.length, data:.data, zero:.zero}' \
  < test-file-extents.tmp > test-file-extents.nbdkit

diff -u test-file-extents.nbdkit test-file-extents.local
