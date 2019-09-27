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
set -e
set -x

fail=0

# Test handling of NBD maximum string length of 4k.

requires qemu-nbd --version

name16=1234567812345678
name64=$name16$name16$name16$name16
name256=$name64$name64$name64$name64
name1k=$name256$name256$name256$name256
name4k=$name1k$name1k$name1k$name1k
almost4k=${name4k%8$name16}

# Test behavior of -e: accept 4k max, reject longer
nbdkit -U - -e $name4k null --run true || fail=1
nbdkit -U - -e a$name4k null --run true && fail=1

# The rest of this test uses the ‘qemu-nbd --list’ option added in qemu 4.0.
if ! qemu-nbd --help | grep -sq -- --list; then
    echo "$0: skipping because qemu-nbd does not support the --list option"
    exit 77
fi

# Test response to NBD_OPT_LIST
nbdkit -U - -e $almost4k null --run 'qemu-nbd --list -k $unixsocket' || fail=1
# FIXME: Right now, we can't accept full 4k length - this should succeed
nbdkit -U - -e $name4k null --run 'qemu-nbd --list -k $unixsocket' && fail=1

exit $fail
