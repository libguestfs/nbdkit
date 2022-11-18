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
requires guestfish --version

# This plugin requires a mkfs command of some sort.
requires mkfs --version

# Note we test both qemu-img info and nbdinfo in order to exercise the
# lesser-used exportname paths in both tools.
requires qemu-img --version
requires_nbdinfo

dir=$(mktemp -d /tmp/nbdkit-test-dir.XXXXXX)
cleanup_fn rm -rf $dir

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="ondemand.pid $sock"
rm -f $files
cleanup_fn rm -f $files

# Run nbdkit.
start_nbdkit -P ondemand.pid -U $sock --log=stderr \
             ondemand dir=$dir size=100M wait=true

# Simply querying an export will create the default and test
# filesystems.
nbdinfo "nbd+unix:///?socket=$sock"
nbdinfo "nbd+unix:///test?socket=$sock"

# Check the filesystems were created.
ls -l $dir
test -f $dir/default
test -f $dir/test

# These should fail because the exportname is invalid.
for e in /bad .bad bad. bad:bad ; do
    if nbdinfo "nbd+unix:///$e?socket=$sock" ||
       qemu-img info "nbd+unix:///$e?socket=$sock" ||
       qemu-img info nbd:unix:$sock:exportname=$e
    then
        echo "$0: expected failure trying to create bad exportname"
        exit 1
    fi
done

# Check the filesystem is persistent.
guestfish --format=raw -a "nbd://?socket=$sock" -m /dev/sda <<EOF
  write /test.txt "hello"
EOF

# This part of the test fails under valgrind for unclear reasons which
# appear to be a bug in valgrind.
if [ "x$NBDKIT_VALGRIND" = "x1" ]; then exit 0; fi

guestfish --ro --format=raw -a "nbd://?socket=$sock" -m /dev/sda <<EOF
  cat /test.txt
EOF
