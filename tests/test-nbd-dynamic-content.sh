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

# This test works with older libnbd, showing that dynamic mode affects
# content.
requires_plugin info
requires nbdsh --version

# Because of macOS SIP misfeature the DYLD_* environment variable
# added by libnbd/run is filtered out and the test won't work.  Skip
# it entirely on Macs.
requires_not test "$(uname)" = "Darwin"

sock1=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
export sock2=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
pid1="test-nbd-dynamic-content.pid1"
pid2="test-nbd-dynamic-content.pid2"
files="$sock1 $sock2 $pid1 $pid2"
rm -f $files
cleanup_fn rm -f $files

# Long-running info server, for content sensitive to export name
start_nbdkit -P $pid1 -U $sock1 info exportname

# Long-running nbd bridge, which should pass export names through
start_nbdkit -P $pid2 -U $sock2 nbd socket=$sock1 dynamic-export=true

long=$(printf %04096d 1)
export e
for e in "" "test" "/" "//" " " "/ " "?" "テスト" "-n" '\\' $'\n' "%%" "$long"
do
  nbdsh -c '
import os

e = os.environ["e"]
h.set_export_name(e)
h.connect_unix(os.environ["sock2"])

size = h.get_size()
assert size == len(e.encode("utf-8"))

# Zero-sized reads are not defined in the NBD protocol.
if size > 0:
   buf = h.pread(size, 0)
   assert buf == e.encode("utf-8")
'
done
