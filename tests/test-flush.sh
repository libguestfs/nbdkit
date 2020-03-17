#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2019-2020 Red Hat Inc.
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

requires nbdsh --version

plugin=.libs/test-flush-plugin.so
requires test -f $plugin

files="test-flush.err"
rm -f $files
cleanup_fn rm -f $files

marker() {
    echo " ** $@" >>test-flush.err
}

marker "Test what happens when a plugin fails .can_flush"
nbdsh -c '
try:
    h.connect_command (["nbdkit", "-s", "-v", "'$plugin'"])
except nbd.Error as ex:
    exit (0)
# If we got here, things are broken
exit (1)
' 2>>test-flush.err

marker "A read-only server still triggers .can_flush, which still fails"
nbdsh -c '
try:
    h.connect_command (["nbdkit", "-s", "-r", "-v", "'$plugin'"])
except nbd.Error as ex:
    exit (0)
# If we got here, things are broken
exit (1)
' 2>>test-flush.err

marker "Disable flush and FUA"
nbdsh -c '
h.connect_command (["nbdkit", "-s", "-v", "'$plugin'", "0"])
assert h.is_read_only () == 0
assert h.can_flush () == 0
assert h.can_fua () == 0
' 2>>test-flush.err

marker "Normal flush, emulated FUA"
nbdsh -c '
h.connect_command (["nbdkit", "-s", "-v", "'$plugin'", "1"])
assert h.is_read_only () == 0
assert h.can_flush () == 1
assert h.can_fua () == 1
h.flush () # expect "handling flush"
h.pwrite (bytearray (512), 0, nbd.CMD_FLAG_FUA) # expect "handling flush"
' 2>>test-flush.err

marker "Normal flush, .can_fua is not consulted or advertised when read-only"
nbdsh -c '
h.connect_command (["nbdkit", "-s", "-r", "-v", "'$plugin'", "1"])
assert h.is_read_only () == 1
assert h.can_flush () == 1
assert h.can_fua () == 0
h.flush () # expect "handling flush"
' 2>>test-flush.err

marker "Unusual return value for .can_flush, native FUA"
nbdsh -c '
h.connect_command (["nbdkit", "-s", "-v", "'$plugin'", "2"])
assert h.is_read_only () == 0
assert h.can_flush () == 1
assert h.can_fua () == 1
h.flush () # expect "handling flush"
h.pwrite (bytearray (512), 0, nbd.CMD_FLAG_FUA) # expect "handling native FUA"
' 2>>test-flush.err

marker "Unusual return value for .can_flush, bogus for .can_fua"
nbdsh -c '
h.connect_command (["nbdkit", "-s", "-v", "'$plugin'", "3"])
try:
    h.connect_command (["nbdkit", "-s", "-r", "-v", "'$plugin'"])
except nbd.Error as ex:
    exit (0)
# If we got here, things are broken
exit (1)
' 2>>test-flush.err

marker "Unusual return value for .can_flush, -r means .can_fua unused"
nbdsh -c '
h.connect_command (["nbdkit", "-s", "-r", "-v", "'$plugin'", "3"])
assert h.is_read_only () == 1
assert h.can_flush () == 1
assert h.can_fua () == 0
h.flush () # expect "handling flush"
' 2>>test-flush.err

cat test-flush.err
diff -u - <(sed -n 's/.*\( \*\*\(handling.*\)\?\).*/\1/p' test-flush.err) <<EOF
 **
 **
 **
 **
 **handling flush
 **handling flush
 **
 **handling flush
 **
 **handling flush
 **handling native FUA
 **
 **
 **handling flush
EOF
