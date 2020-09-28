#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2019 Red Hat Inc.
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

# See also: tests/old-plugins/README.

source ./functions.sh
set -e
set -x

requires guestfish --version
requires cut --version
requires test -f disk

# This script is called with one parameter in the form
# <ARCH>-<OS>-<VERSION> corresponding to a test in
# tests/old-plugins/<ARCH>/<OS>/<VERSION>/
if [ -z "$1" ]; then
    echo "$0: do not call this script directly"
    exit 1
fi
test_arch=$(echo "$1" | cut -d - -f 1)
test_os=$(echo "$1" | cut -d - -f 2)
test_version=$(echo "$1" | cut -d - -f 3-)
d="old-plugins/$test_arch/$test_os/$test_version"
f="$d/nbdkit-file-plugin.so"

# User can delete the directory or plugin file if they want because of
# concerns about distributing binaries in the source.  We simply skip
# the test in this case.
requires test -d "$d"
requires test -f "$f"

# If we're not running on the right arch/OS for this test, skip.
requires test "$(uname -m)" = "$test_arch"
requires test "$(uname -s)" = "$test_os"

disk="$(mktemp /tmp/nbdkit-test-disk.XXXXXX)"
files="$disk"
cleanup_fn rm -f $files

cp disk $disk

nbdkit -fv -U - $f file=$disk \
       --run '
    guestfish \
        add "" protocol:nbd server:unix:$unixsocket : \
        run : \
        mount /dev/sda1 / : \
        write /hello "hello,world" : \
        cat /hello : \
        fstrim /
'
