#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018-2020 Red Hat Inc.
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

# password=- was broken in the VDDK plugin in nbdkit 1.18 through
# 1.20.2.  This was because the reexec code caused
# nbdkit_read_password to be called twice, second time with stdin
# reopened on /dev/null.  Since we have now fixed this bug, this is a
# regression test.

source ./functions.sh
set -e
set -x

# The tests break under valgrind for a couple of reasons:
# (1) Setting LD_LIBRARY_PATH breaks valgrind.
# (2) Valgrinded nbdkit hangs under expect.
if [ "x$NBDKIT_VALGRIND" = "x1" ]; then
    echo "$0: skipped under valgrind"
    exit 77
fi

requires qemu-img --version
requires expect -v

out=test-vddk-password-interactive.out
cleanup_fn rm -f $out

# Get dummy-vddk to print the password to stderr.
export DUMMY_VDDK_PRINT_PASSWORD=1

export out
expect -f - <<'EOF'
  spawn sh -c "
    nbdkit -fv -U - \
           vddk \
           libdir=.libs \
           server=noserver.example.com thumbprint=ab \
           vm=novm /nofile \
           user=root password=- \
           --run 'qemu-img info \$nbd' \
           2>$env(out)"
  expect "ssword:"
  send "abc\r"
  wait
EOF
cat $out

grep "password=abc$" $out
