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

# Demonstrate a fix for a bug where blocksize used to cause extents failures

source ./functions.sh
set -e
set -x

requires_unix_domain_sockets
requires nbdsh --base-allocation -c 'exit(not h.supports_uri())'

# Script a server that requires 512-byte aligned requests, reports only one
# extent at a time, and with a hole placed unaligned to 4k bounds
exts='
if test $3 -gt $(( 32 * 1024 )); then
  echo "EINVAL request too large" 2>&1
  exit 1
fi
if test $(( ($3|$4) & 511 )) != 0; then
  echo "EINVAL request unaligned" 2>&1
  exit 1
fi
type=
if test $(( $4 >= 512 && $4 < 8 * 1024 )) = 1; then
  type=hole,zero
fi
echo $4 512 $type
'

# We also need an nbdsh script to parse all extents, coalescing adjacent
# types for simplicity, as well as testing some unaligned probes.
export script='
size = h.get_size()
offs = 0
entries = []
def f (metacontext, offset, e, err):
    global entries
    global offs
    assert offs == offset
    for length, flags in zip(*[iter(e)] * 2):
        if entries and flags == entries[-1][1]:
            entries[-1] = (entries[-1][0] + length, flags)
        else:
            entries.append((length, flags))
        offs = offs + length

# Test a loop over the entire device
while offs < size:
    h.block_status (size - offs, offs, f)
assert entries == [(4096, 0), (4096, 3), (57344, 0)]

# Unaligned status queries must also work
offs = 1
entries = []
h.block_status (1, offs, f, nbd.CMD_FLAG_REQ_ONE)
assert entries == [(1, 0)]

offs = 512
entries = []
h.block_status (512, offs, f)
assert entries == [(3584, 0)]

offs = 4095
entries=[]
while offs < 4097:
    h.block_status (4097 - offs, offs, f, nbd.CMD_FLAG_REQ_ONE)
assert entries == [(1, 0), (1, 3)]
'

# Now run everything
nbdkit -U - --filter=blocksize eval minblock=4k maxlen=32k \
       get_size='echo 64k' pread='exit 1' extents="$exts" \
       --run 'nbdsh --base-allocation -u "$uri" -c "$script"'
