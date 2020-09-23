#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2019-2020 Red Hat Inc.
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

requires_plugin file
requires nbdinfo --version
requires_nbdsh_uri
requires nbdsh -c 'print(h.set_full_info)'

sock=`mktemp -u`
files="$sock ext2-exportname.pid ext2-exportname.out"
rm -f $files
cleanup_fn rm -f $files

# Set up a long-running server responsive to the client's export name
start_nbdkit -P ext2-exportname.pid -U $sock --filter=ext2 \
    --filter=exportname file ext2.img ext2file=exportname \
    exportname-list=explicit exportname=hidden

# Test that when serving by exportname, our description varies according
# to the client's request.
nbdinfo nbd+unix:///manifest\?socket=$sock > ext2-exportname.out
cat ext2-exportname.out
grep manifest ext2-exportname.out
grep 'content.*ASCII' ext2-exportname.out

nbdinfo nbd+unix:///disks/disk.img\?socket=$sock > ext2-exportname.out
cat ext2-exportname.out
grep disk.img ext2-exportname.out
grep 'content.*MBR' ext2-exportname.out

if nbdinfo nbd+unix://?socket=$sock > ext2-exportname.out; then
    echo "unexpected success"
    exit 1
fi

# Test that there is no export list advertised
nbdinfo --list --json nbd+unix://?socket=$sock > ext2-exportname.out
cat ext2-exportname.out
grep '"exports": \[]' ext2-exportname.out

:
