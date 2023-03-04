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

# Test the file plugin dirfd parameter.

source ./functions.sh
set -e
set -x

requires_plugin file
requires_nbdinfo
requires $TRUNCATE --version

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="file-dirfd.pid $sock"
rm -f $files
cleanup_fn rm -f $files

# Create and populate a directory of disk images.
rm -rf file-dirfd.d
cleanup_fn rm -rf file-dirfd.d
mkdir file-dirfd.d

$TRUNCATE -s 1048576 file-dirfd.d/disk1
$TRUNCATE -s 16384 file-dirfd.d/disk2

# Run nbdkit on the directory.
exec 6<file-dirfd.d
start_nbdkit -P file-dirfd.pid -U $sock file dirfd=6
top_uri="nbd+unix:///?socket=$sock"
disk1_uri="nbd+unix:///disk1?socket=$sock"
disk2_uri="nbd+unix:///disk2?socket=$sock"

nbdinfo --list "$top_uri"
nbdinfo --size "$disk1_uri"
nbdinfo --size "$disk2_uri"
