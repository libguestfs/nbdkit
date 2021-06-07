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

source ./functions.sh
set -e
set -x

requires_plugin ondemand
requires qemu-nbd --version
requires bash -c 'qemu-nbd --help | grep -- --list'

dir=$(mktemp -d /tmp/nbdkit-test-dir.XXXXXX)
cleanup_fn rm -rf $dir

out=test-ondemand-list.out
rm -f $out
cleanup_fn rm -f $out

# Put some files into the exports directory to pretend that we're
# restarting nbdkit after a previous run.
touch $dir/default
touch $dir/export1
touch $dir/export2
touch $dir/export3

export LANG=C
nbdkit -U - ondemand dir=$dir size=1M \
       --run 'qemu-nbd -k $unixsocket -L' > $out
cat $out

# We should have 4 exports, since "default" file is the same as the
# default export.
grep "exports available: 4" $out

# Check the 4 exports are present.
grep "export: ''" $out
grep "export: 'export1'" $out
grep "export: 'export2'" $out
grep "export: 'export3'" $out
