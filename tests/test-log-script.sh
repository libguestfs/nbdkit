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

log=log-script.log
cleanup_fn rm -f $log
rm -f $log

nbdsh -c '
h.connect_command(["nbdkit", "-s",
                   "--filter=log",
                   "memory", "size=1K",
                   "logscript=echo $act $type $connection $id $offset >> log-script.log"])
# Do some reads and writes.
h.pread(512, 0)
h.pwrite(bytearray(512), 0)
h.pwrite(bytearray(512), 512)
h.pread(512, 512)
'

# Print the full log to help with debugging.
cat $log

# Check the script contains the expected operations.
grep '^Read ENTER 1 1 0x0' $log
grep '^Read LEAVE 1 1' $log
grep '^Write ENTER 1 2 0x0' $log
grep '^Write LEAVE 1 2' $log
grep '^Write ENTER 1 3 0x200' $log
grep '^Write LEAVE 1 3' $log
grep '^Read ENTER 1 4 0x200' $log
grep '^Read LEAVE 1 4' $log
