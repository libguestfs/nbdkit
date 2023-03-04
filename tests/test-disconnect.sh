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
set -x

requires_nbdsh_uri

plugin=.libs/test-disconnect-plugin.$SOEXT
requires test -f $plugin

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="disconnect.pid $sock"
cleanup_fn rm -f $files

# Start nbdkit with the disconnect plugin, which has delayed reads and
# does disconnect on write based on export name.
start_nbdkit -P disconnect.pid -U $sock $plugin

pid=`cat disconnect.pid`

# Empty export name does soft disconnect on write; the write and the
# pending read should still succeed, but second read attempt should fail.
nbdsh -u "nbd+unix:///?socket=$sock" -c '
import errno

def waitfor(cookie):
  while True:
    c = h.aio_peek_command_completed()
    if c:
      break
    h.poll(-1)
  assert c == cookie

buf = nbd.Buffer(1)
c1 = h.aio_pread(buf, 1)
c2 = h.aio_pwrite(buf, 2)
waitfor(c2)
h.aio_command_completed(c2)
c3 = h.aio_pread(buf, 3)
waitfor(c3)
try:
  h.aio_command_completed(c3)
  assert False
except nbd.Error as ex:
  assert ex.errnum == errno.ESHUTDOWN
waitfor(c1)
h.aio_command_completed(c1)
h.shutdown()
'

# Non-empty export name does hard disconnect on write. The write and the
# pending read should fail with lost connection.
nbdsh -u "nbd+unix:///a?socket=$sock" -c '
import errno

buf = nbd.Buffer(1)
c1 = h.aio_pread(buf, 1)
c2 = h.aio_pwrite(buf, 2)
while h.aio_in_flight() > 1:
  h.poll(-1)
assert h.aio_is_ready() is False
try:
  h.aio_command_completed(c1)
  assert False
except nbd.Error as ex:
  assert ex.errnum == errno.ENOTCONN
try:
  h.aio_command_completed(c2)
  assert False
except nbd.Error as ex:
  assert ex.errnum == errno.ENOTCONN
'

# nbdkit should still be running
kill -s 0 $pid
