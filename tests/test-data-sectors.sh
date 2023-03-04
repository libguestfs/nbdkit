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

# Test an example from the manual.  Several files of different sizes
# are included, but the final disk must have everything
# sector-aligned.

source ./functions.sh
set -e
set -x

requires_nbdsh_uri
requires $TRUNCATE --version

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="data-sectors.pid sector1 sector2 sector3 $sock"
rm -f $files
cleanup_fn rm -f $files

printf "1" > sector1
printf "2" > sector2
$TRUNCATE -s 1024 sector2
printf "3" > sector3
$TRUNCATE -s 513 sector3

# Run nbdkit.
start_nbdkit -P data-sectors.pid -U $sock \
       data '<sector1 @^512 <sector2 @^512 <sector3 @^512'

nbdsh --connect "nbd+unix://?socket=$sock" \
      -c '
assert h.get_size() == 512 + 1024 + 1024
buf = h.pread(h.get_size(), 0)
print("%r" % buf)
assert buf[0] == 0x31
assert buf[512] == 0x32
assert buf[1536] == 0x33
'
