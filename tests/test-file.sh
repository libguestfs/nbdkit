#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2013-2020 Red Hat Inc.
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

# Test the file plugin.

source ./functions.sh
set -e
set -x

requires_plugin file
requires_nbdsh_uri
requires truncate --version

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="file.pid file.img $sock"
rm -f $files
cleanup_fn rm -f $files

truncate -s 16384 file.img
start_nbdkit -P file.pid -U $sock file file.img

# Try to test all the major functions supported by both
# the Unix and Windows versions of the file plugin.
nbdsh -u "nbd+unix://?socket=$sock" -c '
assert not h.is_read_only()

buf0 = bytearray(1024)
buf1 = b"1" * 1024
buf2 = b"2" * 1024
h.pwrite(buf1 + buf2 + buf1 + buf2, 1024)
buf = h.pread(8192, 0)
assert buf == buf0 + buf1 + buf2 + buf1 + buf2 + buf0*3

if h.can_flush():
   h.flush()

if h.can_trim():
   h.trim(1024, 1024)
   buf = h.pread(8192, 0)
   assert buf == buf0*2 + buf2 + buf1 + buf2 + buf0*3

   # It would be nice to test base:allocation here, but
   # unfortunately it is not practical because it would
   # depend on the host filesystem block size.

h.zero(4096, 1024)
buf = h.pread(8192, 0)
assert buf == buf0*8
'
