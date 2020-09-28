#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2020 François Revol.
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

# Test the ip filter.  Necessarily this is rather limited because we
# are only able to use the loopback connection.

source ./functions.sh
set -e
set -x

requires_nbdsh_uri

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="ddrescue.pid $sock ddrescue.txt ddrescue-test1.map"
rm -f $files
cleanup_fn rm -f $files

rm -f ddrescue.txt
for i in {0..1000}; do
    printf "ddrescue" >> ddrescue.txt
done

echo "#
# current_pos  current_status  current_pass
0x00000000     +               1
#      pos        size  status
0x00000000  0x00000200  +
0x00000200  0x00000200  -
0x00000400  0x00000200  +" > ddrescue-test1.map


# Run nbdkit.
start_nbdkit -P ddrescue.pid -U $sock \
       --filter=ddrescue data \
       ddrescue-mapfile="ddrescue-test1.map"\
       size=1M \
       '@0x000 <ddrescue.txt'

nbdsh --connect "nbd+unix://?socket=$sock" \
      -c '
buf = h.pread(512, 0)
assert buf == b"ddrescue" * 64
try:
    h.pread(512, 512)
    # This should not happen.
    exit(1)
except nbd.Error as ex:
    # Check the errno is expected.
    assert ex.errno == "EIO"
buf = h.pread(512, 2 * 512)
assert buf == b"ddrescue" * 64
'
