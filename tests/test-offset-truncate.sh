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

requires_unix_domain_sockets
requires qemu-img --version
requires nbdsh -c 'exit (not h.supports_uri ())'

function do_test_info ()
{
    nbdkit -U - --filter=offset --filter=truncate pattern size=1024 \
           "$@" --run 'qemu-img info $nbd'
}

function do_test_read512 ()
{
    nbdkit -U - --filter=offset --filter=truncate pattern size=1024 \
           "$@" --run 'nbdsh -u "$uri" -c "h.pread (512, 0)"'
}

function do_test_zero512 ()
{
    nbdkit -U - --filter=offset --filter=truncate memory size=1024 \
           "$@" --run 'nbdsh -u "$uri" -c "h.zero (512, 0)"'
}

function expect_fail ()
{
    if "$@"; then
        echo "$0: expected this test to fail"
        exit 1
    fi
}

# Not a test, just check we can combine the two filters and the test
# harness works.
do_test_info
expect_fail do_test_info bleah

# Test a non-sector-aligned offset.  For nbdkit this should
# not make any difference.
do_test_info offset=1 range=512

# Reading beyond the end of the underlying image, even by 1 byte,
# should fail.
expect_fail do_test_info offset=513 range=512

# Can we extend the image by 1 byte and use the same offset as the
# previous test?  This should be fine.
do_test_info truncate=1025 offset=513 range=512

# Truncate the image.  Offsets larger than the truncation should fail,
# even though the underlying image is big enough.
expect_fail do_test_info truncate=513 offset=2 range=512

# Both truncation and the offset filter range option should also
# prevent operations beyond the end of the truncated size even if the
# underlying image is big enough.
do_test_read512
do_test_zero512
expect_fail do_test_read512 truncate=511
expect_fail do_test_zero512 truncate=511
expect_fail do_test_read512 truncate=511 range=512
expect_fail do_test_zero512 truncate=511 range=512
expect_fail do_test_read512 range=511
expect_fail do_test_zero512 range=511

# Reading from an offset much larger than the underlying image should
# work if we extended it big enough.
do_test_info truncate=4096 offset=2047 range=512

# But an offset even larger should fail.
expect_fail do_test_info truncate=4096 offset=$((4096-511)) range=512

# An offset beyond the end of the disk should fail, whether it's the
# underlying disk or a truncated disk.
expect_fail do_test_info offset=1025 range=512
expect_fail do_test_info truncate=4096 offset=4098 range=512
