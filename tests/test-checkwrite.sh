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

# Run nbdkit + nbdcopy with various plugins with checkwrite on top.

source ./functions.sh
set -e
set -x

requires_run
requires_filter checkwrite

# nbdcopy >= 1.5.9 required for this test.
requires_nbdcopy
requires_libnbd_version 1.5.9

do_test ()
{
    nbdkit -U - -v --filter=checkwrite "$@" --run 'nbdcopy "$uri" "$uri"'
}

# Tests zero-sized disk.
do_test null

# Test of extents.  Should be instantaneous despite the large disk
# because nbdcopy will skip the whole disk.  Choose various disk sizes
# to try to exercise the request and work sizes inside nbdcopy.
do_test memory 1M
do_test memory 256M
do_test memory 10G

# Data disks with a mix of data and holes.
do_test data "@32767 1"
do_test data "@32768 1"
do_test data "@32768 1" size=64K
do_test data "1 @0x100000000 1"

# Non-sparse disk containing fixed pattern.
do_test pattern 1M

# Use of an odd offset here should test corner cases in extent
# handling.
do_test --filter=offset data "1 @0x100000000 1" offset=1

if test -f disk; then
    do_test file disk
fi
