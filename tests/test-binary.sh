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

# Check that the nbdkit command (which should point to the local
# wrapper) eventually runs the locally built server/nbdkit and not
# some other binary.  So we're testing the right thing.  This should
# be ensured by PATH being set in tests/Makefile.am:TESTS_ENVIRONMENT.
# Also checks that nbdkit --dump-config contains binary=<binary>.

source ./functions.sh
set -e

requires $CUT --version

binary1="$( nbdkit --dump-config           | grep ^binary= | $CUT -d= -f2 )"
binary2="$( ../server/nbdkit$EXEEXT --dump-config |
                                             grep ^binary= | $CUT -d= -f2 )"

echo binary1=$binary1
echo binary2=$binary2

if [ "$binary1" != "$binary2" ]; then
    echo "*** TEST ENVIRONMENT PROBLEM ***"
    echo "nbdkit command does not run locally compiled binary"
    echo "check PATH=$PATH"
    exit 1
fi
