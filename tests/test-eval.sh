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

source ./functions.sh
set -e
set -x

requires_unix_domain_sockets
requires qemu-img --version
requires dd iflag=count_bytes </dev/null

files="eval.out eval.missing"
rm -f $files
cleanup_fn rm -f $files

# The sleep after running qemu-img is to try and ensure that the close
# method really runs.  Otherwise there is a race where the connection
# is dropped by qemu-img, the --run command exits, a signal is sent to
# nbdkit, and nbdkit shuts down before the .close callback is called.
nbdkit -U - eval \
       get_size='echo 64M' \
       pread='dd if=/dev/zero count=$3 iflag=count_bytes' \
       missing='echo "in missing: $@" >> eval.missing; exit 2' \
       unload='' \
       --run 'qemu-img info $nbd; sleep 10' > eval.out

cat eval.out
grep '67108864 bytes' eval.out

# Check "missing" was called at least once.
cat eval.missing
grep 'in missing' eval.missing

# Check certain known methods are run.
grep 'in missing: config_complete' eval.missing
grep 'in missing: thread_model' eval.missing
grep 'in missing: can_write' eval.missing
grep 'in missing: is_rotational' eval.missing
grep 'in missing: get_ready' eval.missing
grep 'in missing: after_fork' eval.missing
grep 'in missing: preconnect' eval.missing
grep 'in missing: open' eval.missing
grep 'in missing: close' eval.missing
