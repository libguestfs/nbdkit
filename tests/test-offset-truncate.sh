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

# Test offset and truncate filter and various corner cases and error
# handling between layers.

source ./functions.sh
set -e
set -x

requires qemu-img --version

function do_test ()
{
    nbdkit -U - --filter=offset --filter=truncate pattern size=1024 \
           "$@" --run 'qemu-img info $nbd'
}

function expected_fail ()
{
    echo "$0: expected this test to fail"
    exit 1
}

# Not a test, just check we can combine the two filters and the test
# harness works.
do_test
if do_test bleah ; then expected_fail; fi

# Test a non-sector-aligned offset.  For nbdkit this should
# not make any difference.
do_test offset=1 range=512

# Reading beyond the end of the underlying image, even by 1 byte,
# should fail.
if do_test offset=513 range=512; then expected_fail; fi

# Can we extend the image by 1 byte and use the same offset as the
# previous test?  This should be fine.
do_test truncate=1025 offset=513 range=512

# Truncate the image.  Offsets larger than the truncation should fail,
# even though the underlying image is big enough.
if do_test truncate=513 offset=2 range=512; then expected_fail; fi

# Reading from an offset much larger than the underlying image should
# work if we extended it big enough.
do_test truncate=4096 offset=2047 range=512

# But an offset even larger should fail.
if do_test truncate=4096 offset=$((4096-511)) range=512; then expected_fail; fi

# An offset beyond the end of the disk should fail, whether it's the
# underlying disk or a truncated disk.
if do_test offset=1025 range=512; then expected_fail; fi
if do_test truncate=4096 offset=4098 range=512; then expected_fail; fi
