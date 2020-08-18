#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2014-2020 Red Hat Inc.
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

# Test nbdkit --run (captive nbdkit) option.

source ./functions.sh
set -e
set -x

requires_unix_domain_sockets

fail=0

sock=`mktemp -u`
files="$sock captive.out captive.pid"
rm -f $files
cleanup_fn rm -f $files

nbdkit -U $sock example1 --run '
    echo nbd=$nbd; echo port=$port; echo socket=$unixsocket
  ' > captive.out

# Check the output.
if [ "$(cat captive.out)" != "nbd=nbd:unix:$sock
port=
socket=$sock" ]; then
    echo "$0: unexpected output"
    cat captive.out
    fail=1
fi

# Check that a failed --run process affects exit status
status=0
nbdkit -U - example1 --run 'exit 2' > captive.out || status=$?
if test $status != 2; then
    echo "$0: unexpected exit status $status"
    fail=1
fi
if test -s captive.out; then
    echo "$0: unexpected output"
    cat captive.out
    fail=1
fi

# Check that nbdkit death from unhandled signal affects exit status
status=0
nbdkit -U - -P captive.pid example1 --run '
for i in {1..60}; do
    if test -s captive.pid; then break; fi
    sleep 1
done
if ! test -s captive.pid; then
    echo "$0: no pidfile yet"
    exit 10
fi
kill -s ABRT $(cat captive.pid) || exit 10
sleep 10
' > captive.out || status=$?
if test $status != $(( 128 + $(kill -l ABRT) )); then
    echo "$0: unexpected exit status $status"
    fail=1
fi
if test -s captive.out; then
    echo "$0: unexpected output"
    cat captive.out
    fail=1
fi

exit $fail
