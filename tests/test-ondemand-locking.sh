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

# Test mutual exclusion between clients with the ondemand plugin.

source ./functions.sh
set -e
set -x

requires_plugin ondemand
requires qemu-img --version
requires nbdsh --version

dir=`mktemp -d`
cleanup_fn rm -rf $dir

sock=`mktemp -u`
files="ondemand-locking.pid $sock"
rm -f $files
cleanup_fn rm -f $files

# Run nbdkit.
start_nbdkit -P ondemand-locking.pid -U $sock --log=stderr \
             ondemand dir=$dir size=100M

export sock
nbdsh -c - <<'EOF'
import os
sock = os.environ["sock"]

# Open export1 with the default nbdsh handle.
# This should take an exclusive lock.
h.set_export_name("export1")
h.connect_unix(sock)

# Trying to reopen export1 on a new handle should fail.
h2 = nbd.NBD()
h2.set_export_name("export1")
try:
    h2.connect_unix(sock)
except nbd.Error:
    pass

# But opening a different export should succeed.
h3 = nbd.NBD()
h3.set_export_name("export2")
h3.connect_unix(sock)
EOF
