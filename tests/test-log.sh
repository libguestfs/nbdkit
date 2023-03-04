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

requires nbdsh --version
requires_filter log

cleanup_fn rm -f log.log
rm -f log.log

echo '# My log' > log.log

nbdsh -c '
h.connect_command(["nbdkit", "-s", "--filter=log", "null", "size=10M",
                   "logfile=log.log", "logappend=1"])
mb = 1024*1024
ba = b"x"*(2*mb)
h.pwrite(ba, mb)
h.pread(mb, mb*2)
'

# Print the full log to help with debugging.
cat log.log

# The log should have been appended, preserving our marker.
grep '# My log' log.log

# The log should show Ready.
grep ' Ready ' log.log

# The log should _not_ show Fork, because the server didn't fork.
grep -v ' Fork ' log.log

# The log should show Preconnect, Connect and Disconnect stages.
grep ' Preconnect ' log.log
grep ' Connect ' log.log
grep ' Disconnect ' log.log

# The log should show a write and read.
grep 'connection=1 Write id=1 offset=0x100000 count=0x200000 ' log.log
grep 'connection=1 Read id=2 offset=0x200000 count=0x100000 ' log.log
