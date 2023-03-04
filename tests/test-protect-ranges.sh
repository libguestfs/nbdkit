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

requires_run

# This tests the code handling ranges/regions.  We're not testing
# access, simply that the code which coalesces ranges and turns them
# into regions doesn't crash.

cmd="nbdkit -U - --filter=protect null size=1M --run true"

$cmd

$cmd protect=-511
$cmd protect=512-
$cmd protect=512- protect=1024-
$cmd protect=-
$cmd protect=- protect=-

$cmd protect=0-511
$cmd protect=0-511 protect=512-1023
$cmd protect=0-511 protect=512-1023 protect=1024-2047
$cmd protect=0-511 protect=0-511
$cmd protect=0-511 protect=0-512
$cmd protect=0-511 protect=1-512
$cmd protect=0-511 protect=1-512 protect=2-1023
$cmd protect=0-511 protect=1-1023
$cmd protect=512-1023 protect=0-1023
$cmd protect=512-1023 protect=0-2047
$cmd protect=-511 protect=512-
$cmd protect=-511 protect=1024-
$cmd protect=- protect=512-1023

# Complemented ranges.
$cmd protect=~512-1023
$cmd protect=0-511 protect=~512-1023 protect=1024-
$cmd 'protect=~-' 'protect=~-'
