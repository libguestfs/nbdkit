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

# Check shutdown/disconnect behavior triggered by special status values

source ./functions.sh
set -e
set -x

requires_plugin eval
requires_nbdsh_uri

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="eval-disconnect.pid $sock"
rm -f $files
cleanup_fn rm -f $files

# Start nbdkit with a plugin that fails writes according to the export
# name which must be numeric: a positive value leaves stderr empty, a
# non-positive one outputs EPERM first.  Serve multiple clients.
serve()
{
    rm -f $files
    start_nbdkit -P eval-disconnect.pid -U $sock eval \
        get_size=' echo 1024 ' \
        open=' if test $3 -le 0; then \
                 echo EPERM > $tmpdir/err; echo $((-$3)); \
               else \
                 : > $tmpdir/err; echo $3; \
               fi ' \
        flush=' exit 0 ' \
        pwrite=' cat >/dev/null; cat $tmpdir/err >&2; exit $2 '
}
check_dead()
{
    # Server should die shortly, if not already dead at this point.
    for (( i=0; i < 5; i++ )); do
        kill -s 0 "$(cat eval-disconnect.pid)" || break
        sleep 1
    done
    if [ $i = 5 ]; then
        echo "nbdkit didn't exit fast enough"
        exit 1
    fi
}
serve

# Noisy status 0 (OK) succeeds, despite text to stderr
nbdsh -u "nbd+unix:///0?socket=$sock" -c - <<\EOF
h.pwrite(b"1", 0)
h.flush()
h.shutdown()
EOF

# Silent status 1 (ERROR) fails; nbdkit turns lack of error into EIO
nbdsh -u "nbd+unix:///1?socket=$sock" -c - <<\EOF
import errno
try:
  h.pwrite(b"1", 0)
  assert False
except nbd.Error as ex:
  assert ex.errnum == errno.EIO
h.flush()
h.shutdown()
EOF

# Noisy status 1 (ERROR) fails with supplied EPERM
nbdsh -u "nbd+unix:///-1?socket=$sock" -c - <<\EOF
import errno
try:
  h.pwrite(b"1", 0)
  assert False
except nbd.Error as ex:
  assert ex.errnum == errno.EPERM
h.flush()
h.shutdown()
EOF

# Silent status 4 (SHUTDOWN_OK) kills the server.  It is racy whether client
# sees success or if the connection is killed with libnbd giving ENOTCONN
nbdsh -u "nbd+unix:///4?socket=$sock" -c - <<\EOF
import errno
try:
  h.pwrite(b"1", 0)
except nbd.Error as ex:
  assert ex.errnum == errno.ENOTCONN
EOF
check_dead
serve

# Noisy status 4 (SHUTDOWN_OK) kills the server.  It is racy whether client
# sees success or if the connection is killed with libnbd giving ENOTCONN
nbdsh -u "nbd+unix:///-4?socket=$sock" -c - <<\EOF
import errno
try:
  h.pwrite(b"1", 0)
except nbd.Error as ex:
  assert ex.errnum == errno.ENOTCONN
EOF
check_dead
serve

# Silent status 5 (SHUTDOWN_ERR) kills the server.  It is racy whether client
# sees implied ESHUTDOWN or if the connection dies with libnbd giving ENOTCONN
nbdsh -u "nbd+unix:///5?socket=$sock" -c - <<\EOF
import errno
try:
  h.pwrite(b"1", 0)
  assert False
except nbd.Error as ex:
  assert ex.errnum == errno.ESHUTDOWN or ex.errnum == errno.ENOTCONN
EOF
check_dead
serve

# Noisy status 5 (SHUTDOWN_ERR) kills the server.  It is racy whether client
# sees EPERM or if the connection is killed with libnbd giving ENOTCONN
nbdsh -u "nbd+unix:///-5?socket=$sock" -c - <<\EOF
import errno
try:
  h.pwrite(b"1", 0)
  assert False
except nbd.Error as ex:
  assert ex.errnum == errno.EPERM or ex.errnum == errno.ENOTCONN
EOF
check_dead
serve

# Silent status 6 (DISC_FORCE) kills socket; libnbd detects as ENOTCONN
nbdsh -u "nbd+unix:///6?socket=$sock" -c - <<\EOF
import errno
try:
  h.pwrite(b"1", 0)
  assert False
except nbd.Error as ex:
  assert ex.errnum == errno.ENOTCONN
assert h.aio_is_ready() is False
EOF

# Noisy status 6 (DISC_FORCE) kills socket; libnbd detects as ENOTCONN
nbdsh -u "nbd+unix:///-6?socket=$sock" -c - <<\EOF
import errno
try:
  h.pwrite(b"1", 0)
  assert False
except nbd.Error as ex:
  assert ex.errnum == errno.ENOTCONN
assert h.aio_is_ready() is False
EOF

# Silent status 7 (DISC_SOFT_OK) succeeds, but next command gives ESHUTDOWN
nbdsh -u "nbd+unix:///7?socket=$sock" -c - <<\EOF
import errno
h.pwrite(b"1", 0)
try:
  h.flush()
  assert False
except nbd.Error as ex:
  assert ex.errnum == errno.ESHUTDOWN
h.shutdown()
EOF

# Noisy status 7 (DISC_SOFT_OK) succeeds, but next command gives ESHUTDOWN
nbdsh -u "nbd+unix:///-7?socket=$sock" -c - <<\EOF
import errno
h.pwrite(b"1", 0)
try:
  h.flush()
  assert False
except nbd.Error as ex:
  assert ex.errnum == errno.ESHUTDOWN
h.shutdown()
EOF

# Silent status 8 (DISC_SOFT_ERR) fails with implied ESHUTDOWN, then next
# command gives ESHUTDOWN
nbdsh -u "nbd+unix:///8?socket=$sock" -c - <<\EOF
import errno
try:
  h.pwrite(b"1", 0)
  assert False
except nbd.Error as ex:
  assert ex.errnum == errno.ESHUTDOWN
try:
  h.flush()
  assert False
except nbd.Error as ex:
  assert ex.errnum == errno.ESHUTDOWN
h.shutdown()
EOF

# Noisy status 8 (DISC_SOFT_ERR) fails with explicit EPERM, then next
# command gives ESHUTDOWN
nbdsh -u "nbd+unix:///-8?socket=$sock" -c - <<\EOF
import errno
try:
  h.pwrite(b"1", 0)
  assert False
except nbd.Error as ex:
  assert ex.errnum == errno.EPERM
try:
  h.flush()
  assert False
except nbd.Error as ex:
  assert ex.errnum == errno.ESHUTDOWN
h.shutdown()
EOF
