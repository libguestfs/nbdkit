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

# Test the full plugin.  Note that this plugin causes libguestfs to
# hang so we have to use low level tools.

source ./functions.sh
set -e

requires_nbdsh_uri

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="full.pid $sock full.out"
rm -f $files
cleanup_fn rm -f $files

# Run nbdkit with the full plugin.
start_nbdkit -P full.pid -U $sock full 1M

# All reads should succeed.
nbdsh --connect "nbd+unix://?socket=$sock" \
      -c 'h.pread(512, 0)' \
      -c 'h.pread(512, 512)' \
      -c 'h.pread(512, 1048064)'

# All writes should fail with the ENOSPC error.
nbdsh --connect "nbd+unix://?socket=$sock" \
      -c '
def test(offset):
    try:
        h.pwrite(bytearray(512), offset)
        # This should not happen.
        exit(1)
    except nbd.Error as ex:
        # Check the errno is expected.
        assert ex.errno == "ENOSPC"

test(0)
test(1048064)
'
