#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2020 Red Hat Inc.
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

# Test cc plugin for C++ plugins.

source ./functions.sh

set -e
set -x

if test "$SRCDIR" = ""; then
    echo "$0: \$SRCDIR is not set"
    exit 1
fi

script=$SRCDIR/test-cc-cpp.cpp
if test ! -f "$script"; then
    echo "$0: could not locate test-cc-cpp.cpp"
    exit 1
fi

requires_plugin cc
requires $CXX --version
requires_nbdsh_uri
requires_nbdinfo

out=test-cc-cpp.out
cleanup_fn rm -f $out
rm -f $out

nbdkit -U - cc $script \
       CC="$CXX" \
       EXTRA_CFLAGS="-I$SRCDIR/../include" \
       --run 'nbdinfo --size $uri' > $out
test "$(cat $out)" -eq $((100 * 1024 * 1024))
