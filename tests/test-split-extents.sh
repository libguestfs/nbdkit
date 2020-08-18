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

# Concatenating sparse and data files should have observable extents.

source ./functions.sh
set -e
set -x

requires_unix_domain_sockets
requires nbdsh --base-allocation -c 'exit(not h.supports_uri())'
requires truncate --help
requires stat --help

files="test-split-extents.1 test-split-extents.2"
rm -f $files
cleanup_fn rm -f $files

# Create two files, each half data and half sparse
truncate --size=512k test-split-extents.1
if test "$(stat -c %b test-split-extents.1)" != 0; then
    echo "$0: unable to create sparse file, skipping this test"
    exit 77
fi
printf %$((512*1024))d 1 >> test-split-extents.1
printf %$((512*1024))d 1 >> test-split-extents.2
truncate --size=1M test-split-extents.2

# Test the split plugin
nbdkit -v -U - split test-split-extents.1 test-split-extents.2 \
       --run 'nbdsh --base-allocation --uri "$uri" -c "
entries = []
def f (metacontext, offset, e, err):
    global entries
    assert err.value == 0
    assert metacontext == nbd.CONTEXT_BASE_ALLOCATION
    entries = e
h.block_status (2 * 1024 * 1024, 0, f)
assert entries == [ 512 * 1024, 3,
                    1024 * 1024, 0,
                    512 * 1024, 3 ]
entries = []
# With req one, extents stop at file boundaries
h.block_status (1024 * 1024, 768 * 1024, f, nbd.CMD_FLAG_REQ_ONE)
assert entries == [ 256 * 1024, 0 ]
       "'
