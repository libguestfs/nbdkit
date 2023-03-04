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

# Test the ip filter with pid: parameter.

source ./functions.sh
set -e
set -x

requires_nbdinfo
requires nbdsh --version
requires_nbdsh_uri
# This requires Linux.
requires_linux_kernel_version 2.6

# This is expected to fail because the shell ($$) is not connecting to
# the server.
if nbdkit -U - -v -D ip.rules=1 --filter=ip null allow=pid:$$ deny=all \
          --run 'nbdinfo --size "$uri"'; then
    echo "$0: expected test to fail"
    exit 1
fi

# This is expected to work because we can deny the shell.
nbdkit -U - -v -D ip.rules=1 --filter=ip null deny=pid:$$ \
       --run 'nbdinfo --size "$uri"'

# This is a better test using nbdsh and passing the PID of nbdsh
# itself to nbdkit.  Note this only works because nbd_connect_command
# uses a socketpair which is a kind of nameless Unix domain socket.
nbdsh -c - <<'EOF'
import os

h.connect_command(["nbdkit", "-s",
                   "-v", "-D", "ip.rules=1",
                   "null", "size=512",
                   "--filter=ip",
                   "allow=pid:" + str(os.getpid()),
                   "deny=all"])
assert h.get_size() == 512
EOF
