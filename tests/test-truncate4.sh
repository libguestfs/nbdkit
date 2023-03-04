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

# Regression test when next->get_size changes between connections.
#
# For now, NBD does not support dynamic resize; but the file plugin
# reads size from the file system for each new connection, at which
# point the client remembers that size for the life of the connection.
#
# We are testing that connection A can still see the tail of a file,
# even when connection B is opened while the file was temporarily
# shorter.  If the actions of connection B affect the size visible
# through connection A, we didn't isolate per-connection state.

source ./functions.sh
set -e
set -x

requires nbdsh --version

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
data=truncate4.data
files="truncate4.pid $sock $data"
rm -f $files
cleanup_fn rm -f $files

# Create and truncate the file.
: > $data

# Run nbdkit with file plugin and truncate filter in front.
start_nbdkit -P truncate4.pid -U $sock \
       --filter=truncate \
       file $data \
       round-up=1024

export data sock
nbdsh -c '
import os

data = os.environ["data"]
sock = os.environ["sock"]

def restore_file():
    # Original test data, 1024 bytes of "TEST" repeated.
    with open(data, "w") as file:
        file.write("TEST"*256)

restore_file()

print("Connection A.", flush=True)
connA = nbd.NBD()
connA.set_handle_name("A")
connA.connect_unix(sock)
print("Check the size.", flush=True)
assert connA.get_size() == 1024

print("Truncate %s to 512 bytes." % data, flush=True)
os.truncate(data, 512)

print("Connection B.", flush=True)
connB = nbd.NBD()
connB.set_handle_name("B")
connB.connect_unix(sock)
print("Check the size.", flush=True)
assert connB.get_size() == 1024 # because of the round-up parameter
print("Read data from connection B.", flush=True)
buf = connB.pread(1024, 0)
assert buf == b"TEST"*128 + b"\0"*512

print("Restore the file size and original data.", flush=True)
restore_file()

print("Read data from connection A.", flush=True)
buf = connA.pread(1024, 0)
assert 1024 == len(buf)
assert buf == b"TEST"*256
'
