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

# Test the file plugin on readonly files.

source ./functions.sh
set -e
set -x

requires_non_root
requires_plugin file
requires_nbdsh_uri
requires $TRUNCATE --version

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="file-readonly.pid file-readonly.img $sock"
rm -f $files
cleanup_fn rm -f $files

$TRUNCATE -s 16384 file-readonly.img
chmod a-w file-readonly.img
start_nbdkit -P file-readonly.pid -U $sock file file-readonly.img

# Try to test all the major functions supported by both
# the Unix and Windows versions of the file plugin.
nbdsh -u "nbd+unix://?socket=$sock" -c '
assert h.is_read_only()
assert not h.can_zero()

buf = h.pread(8192, 0)
assert buf == b"\0" * 8192

if h.can_flush():
   h.flush()
'
