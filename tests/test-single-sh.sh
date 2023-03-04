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

requires_single_mode
requires nbdsh --version
requires dd iflag=count_bytes </dev/null

files="single-sh.script single-sh.log"
rm -f $files

cleanup_fn rm -f $files

fail=0
# Inline scripts are incompatible with -s
if nbdkit -s sh - >/dev/null <<EOF
echo "oops: should not have run '$@'" >>single-sh.log
EOF
then
    echo "$0: failed to diagnose -s vs. 'sh -'"
    fail=1
fi
if test -f single-sh.log; then
    echo "$0: script unexpectedly ran"
    cat single-sh.log
    fail=1
fi

cat >single-sh.script <<\EOF
case $1 in
  get_size) echo 1m ;;
  pread) dd if=/dev/zero count=$3 iflag=count_bytes ;;
  *) exit 2 ;;
esac
EOF
chmod +x single-sh.script

# The sh plugin sets up pipes to handle stdin/out per each run of the
# script, but this is not incompatible with using -s for the client
nbdsh -c '
h.connect_command(["nbdkit", "-s", "sh", "single-sh.script"])
assert h.get_size() == 1024 * 1024
buf1 = h.pread(512, 0)
buf2 = bytearray(512)
assert buf1 == buf2
'

exit $fail
