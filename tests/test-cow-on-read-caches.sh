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
requires_filter delay
requires_nbdsh_uri

# On Windows, calling ftruncate in the cow filter fails with:
# nbdkit: memory[1]: error: ftruncate: File too large
if is_windows; then
   echo "$0: the cow filter needs to be fixed to work on Windows"
   exit 77
fi

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="$sock cow-on-read-caches.pid"
rm -f $files
cleanup_fn rm -f $files

# Run nbdkit with the cow filter, cow-on-read and a read delay.
start_nbdkit -P cow-on-read-caches.pid -U $sock \
             --filter=cow --filter=delay \
             memory 64K cow-on-read=true rdelay=10

nbdsh --connect "nbd+unix://?socket=$sock" \
      -c '
from time import time

# First read should suffer a penalty.  Because we are reading
# a single 64K block (same size as the COW block), we should
# only suffer one penalty of approx. 10 seconds.
st = time()
zb = h.pread(65536, 0)
et = time()
el = et-st
print("elapsed time: %g" % el)
assert et-st >= 10
assert zb == bytearray(65536)

# Second read should not suffer a penalty.
st = time()
zb = h.pread(65536, 0)
et = time()
el = et-st
print("elapsed time: %g" % el)
assert el < 10
assert zb == bytearray(65536)

# Write something.
buf = b"abcd" * 16384
h.pwrite(buf, 0)

# Reading back should be quick since it is stored in the overlay.
st = time()
buf2 = h.pread(65536, 0)
et = time()
el = et-st
print("elapsed time: %g" % el)
assert el < 10
assert buf == buf2
'
