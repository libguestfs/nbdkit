#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018 Red Hat Inc.
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

# Test the pattern plugin with the largest possible size supported
# by qemu and nbdkit.

source ./functions.sh
set -e

requires_unix_domain_sockets
requires qemu-io --version

sock=`mktemp -u`
files="pattern-largest-for-qemu.out pattern-largest-for-qemu.pid $sock"
rm -f $files
cleanup_fn rm -f $files

# Run nbdkit with pattern plugin.
# size = (2^63-1) & ~511 which is the largest supported by qemu.
start_nbdkit -P pattern-largest-for-qemu.pid -U $sock \
       pattern 9223372036854775296

qemu-io -r -f raw "nbd+unix://?socket=$sock" \
        -c 'r -v 9223372036854774784 512' | grep -E '^[[:xdigit:]]+:' > pattern-largest-for-qemu.out
if [ "$(cat pattern-largest-for-qemu.out)" != "7ffffffffffffc00:  7f ff ff ff ff ff fc 00 7f ff ff ff ff ff fc 08  ................
7ffffffffffffc10:  7f ff ff ff ff ff fc 10 7f ff ff ff ff ff fc 18  ................
7ffffffffffffc20:  7f ff ff ff ff ff fc 20 7f ff ff ff ff ff fc 28  ................
7ffffffffffffc30:  7f ff ff ff ff ff fc 30 7f ff ff ff ff ff fc 38  .......0.......8
7ffffffffffffc40:  7f ff ff ff ff ff fc 40 7f ff ff ff ff ff fc 48  ...............H
7ffffffffffffc50:  7f ff ff ff ff ff fc 50 7f ff ff ff ff ff fc 58  .......P.......X
7ffffffffffffc60:  7f ff ff ff ff ff fc 60 7f ff ff ff ff ff fc 68  ...............h
7ffffffffffffc70:  7f ff ff ff ff ff fc 70 7f ff ff ff ff ff fc 78  .......p.......x
7ffffffffffffc80:  7f ff ff ff ff ff fc 80 7f ff ff ff ff ff fc 88  ................
7ffffffffffffc90:  7f ff ff ff ff ff fc 90 7f ff ff ff ff ff fc 98  ................
7ffffffffffffca0:  7f ff ff ff ff ff fc a0 7f ff ff ff ff ff fc a8  ................
7ffffffffffffcb0:  7f ff ff ff ff ff fc b0 7f ff ff ff ff ff fc b8  ................
7ffffffffffffcc0:  7f ff ff ff ff ff fc c0 7f ff ff ff ff ff fc c8  ................
7ffffffffffffcd0:  7f ff ff ff ff ff fc d0 7f ff ff ff ff ff fc d8  ................
7ffffffffffffce0:  7f ff ff ff ff ff fc e0 7f ff ff ff ff ff fc e8  ................
7ffffffffffffcf0:  7f ff ff ff ff ff fc f0 7f ff ff ff ff ff fc f8  ................
7ffffffffffffd00:  7f ff ff ff ff ff fd 00 7f ff ff ff ff ff fd 08  ................
7ffffffffffffd10:  7f ff ff ff ff ff fd 10 7f ff ff ff ff ff fd 18  ................
7ffffffffffffd20:  7f ff ff ff ff ff fd 20 7f ff ff ff ff ff fd 28  ................
7ffffffffffffd30:  7f ff ff ff ff ff fd 30 7f ff ff ff ff ff fd 38  .......0.......8
7ffffffffffffd40:  7f ff ff ff ff ff fd 40 7f ff ff ff ff ff fd 48  ...............H
7ffffffffffffd50:  7f ff ff ff ff ff fd 50 7f ff ff ff ff ff fd 58  .......P.......X
7ffffffffffffd60:  7f ff ff ff ff ff fd 60 7f ff ff ff ff ff fd 68  ...............h
7ffffffffffffd70:  7f ff ff ff ff ff fd 70 7f ff ff ff ff ff fd 78  .......p.......x
7ffffffffffffd80:  7f ff ff ff ff ff fd 80 7f ff ff ff ff ff fd 88  ................
7ffffffffffffd90:  7f ff ff ff ff ff fd 90 7f ff ff ff ff ff fd 98  ................
7ffffffffffffda0:  7f ff ff ff ff ff fd a0 7f ff ff ff ff ff fd a8  ................
7ffffffffffffdb0:  7f ff ff ff ff ff fd b0 7f ff ff ff ff ff fd b8  ................
7ffffffffffffdc0:  7f ff ff ff ff ff fd c0 7f ff ff ff ff ff fd c8  ................
7ffffffffffffdd0:  7f ff ff ff ff ff fd d0 7f ff ff ff ff ff fd d8  ................
7ffffffffffffde0:  7f ff ff ff ff ff fd e0 7f ff ff ff ff ff fd e8  ................
7ffffffffffffdf0:  7f ff ff ff ff ff fd f0 7f ff ff ff ff ff fd f8  ................" ]
then
    echo "$0: unexpected pattern:"
    cat pattern-largest-for-qemu.out
    exit 1
fi
