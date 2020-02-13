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

source ./functions.sh
set -e
set -x

rm -f test-vddk.out
cleanup_fn rm -f test-vddk.out

nbdkit vddk libdir=.libs --dump-plugin > test-vddk.out
cat test-vddk.out

grep ^vddk_default_libdir= test-vddk.out

# Also test our magic file= handling, even though the dummy driver doesn't
# really open a file.
# really open a file.  We also ensure that LD_LIBRARY_PATH in the child
# is not further modified, even if nbdkit had to re-exec.  It's tricky,
# though: when running uninstalled, our wrapper nbdkit also modifies
# LD_LIBRARY_PATH, so we need to capture an expected value from what
# leaks through an innocuous plugin.
expect_LD_LIBRARY_PATH=$(nbdkit -U - zero --run 'echo "$LD_LIBRARY_PATH"')
export expect_LD_LIBRARY_PATH

nbdkit -U - vddk libdir=.libs /dev/null \
   --run 'echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
          echo "expect_LD_LIBRARY_PATH=$expect_LD_LIBRARY_PATH"
          test "$LD_LIBRARY_PATH" = "$expect_LD_LIBRARY_PATH"'
