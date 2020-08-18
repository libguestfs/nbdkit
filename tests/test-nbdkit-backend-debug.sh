#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2019 Red Hat Inc.
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
set -x
set -e

requires_unix_domain_sockets
requires qemu-img --version

out="test-nbdkit-backend-debug.out"
debug="test-nbdkit-backend-debug.debug"
files="$out $debug"
rm -f $files
cleanup_fn rm -f $files

nbdkit -U - \
       -v \
       --filter=nofilter \
       memory 10M \
       --run "qemu-img convert \$nbd $out" |& tee $debug

# Should contain all debugging messages.
grep '^nbdkit:.*debug: nofilter: open' $debug
grep '^nbdkit:.*debug: memory: open' $debug
grep '^nbdkit:.*debug: nofilter: pread' $debug
grep '^nbdkit:.*debug: memory: pread' $debug

nbdkit -U - \
       -v -D nbdkit.backend.controlpath=0 \
       --filter=nofilter \
       memory 10M \
       --run "qemu-img convert \$nbd $out" |& tee $debug

# Should contain only datapath messages.
grep -v '^nbdkit:.*debug: nofilter: open' $debug
grep -v '^nbdkit:.*debug: memory: open' $debug
grep '^nbdkit:.*debug: nofilter: pread' $debug
grep '^nbdkit:.*debug: memory: pread' $debug

nbdkit -U - \
       -v -D nbdkit.backend.datapath=0 \
       --filter=nofilter \
       memory 10M \
       --run "qemu-img convert \$nbd $out" |& tee $debug

# Should contain only controlpath messages.
grep '^nbdkit:.*debug: nofilter: open' $debug
grep '^nbdkit:.*debug: memory: open' $debug
grep -v '^nbdkit:.*debug: nofilter: pread' $debug
grep -v '^nbdkit:.*debug: memory: pread' $debug
