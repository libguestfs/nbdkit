#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2019-2022 Red Hat Inc.
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

requires nbdsh -c 'exit(not h.supports_tls())'

# Does the nbdkit binary support TLS?
if ! nbdkit --dump-config | grep -sq tls=yes; then
    echo "$0: nbdkit built without TLS support"
    exit 77
fi

# Did we create the PSK keys file?
# Probably 'certtool' is missing.
if [ ! -s keys.psk ]; then
    echo "$0: PSK keys file was not created by the test harness"
    exit 77
fi

plugin=.libs/test-disconnect-plugin.$SOEXT
requires test -f $plugin

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="disconnect-tls.pid $sock"
cleanup_fn rm -f $files

# Start nbdkit with the disconnect plugin, which has delayed reads and
# does disconnect on write based on export name.
start_nbdkit -P disconnect-tls.pid --tls require --tls-psk=keys.psk \
             -U $sock $plugin

pid=`cat disconnect-tls.pid`

# We can't use 'nbdsh -u "$uri" because of nbd_set_uri_allow_local_file.
# Empty export name does soft disconnect on write; the write and the
# pending read should still succeed, but second read attempt should fail.
nbdsh -c '
import errno

h.set_tls(nbd.TLS_REQUIRE)
h.set_tls_psk_file("keys.psk")
h.set_tls_username("qemu")
h.connect_unix("'"$sock"'")

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
nbdsh -c '
import errno

h.set_tls(nbd.TLS_REQUIRE)
h.set_tls_psk_file("keys.psk")
h.set_tls_username("qemu")
h.set_export_name("a")
h.connect_unix("'"$sock"'")

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
