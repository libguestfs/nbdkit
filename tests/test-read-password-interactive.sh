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
set -e
set -x

# This is an executable C script using nbdkit-cc-plugin.
plugin=$SRCDIR/test-read-password-plugin.c
requires test -x $plugin

requires_unix_domain_sockets
requires expect -v

# Since we are matching on error messages.
export LANG=C

f=test-read-password-interactive.file
out=test-read-password-interactive.out
rm -f $f $out
cleanup_fn rm -f $f $out

# Reading a password from stdin (non-interactive) should fail.
if $plugin -U - -fv file=$f password=- </dev/null >&$out ; then
    echo "$0: expected password=- to fail"
    exit 1
fi
cat $out
grep "stdin is not a tty" $out

export plugin f

# Password read interactively from stdin tty.
expect -f - <<'EOF'
  spawn $env(plugin) -U - -fv password=- file=$env(f)
  expect "ssword:"
  send "abc\r"
  wait
EOF
grep '^abc$' $f

# Empty password read interactively from stdin tty.
rm -f $f
expect -f - <<'EOF'
  spawn $env(plugin) -U - -fv password=- file=$env(f)
  expect "ssword:"
  send "\r"
  wait
EOF
test -f $f
! test -s $f
