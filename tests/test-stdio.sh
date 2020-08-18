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
set -xe

requires nbdsh -c 'exit (not h.supports_uri ())'

plugin=.libs/test-stdio-plugin.$SOEXT
requires test -f $plugin

sock1=`mktemp -u`
sock2=`mktemp -u`
files="test-stdio.in test-stdio.out test-stdio.err
  test-stdio.pid1 test-stdio.pid2 $sock1 $sock2"
rm -f $files
cleanup_fn rm -f $files

# Using a seekable file lets us prove that if the plugin consumes less
# than the full input, the next process sees the rest
cat >test-stdio.in <<EOF
string1
string2
EOF

# .dump_plugin using stdout is normal; using stdin is odd, but viable
{ nbdkit --dump-plugin $plugin; printf 'rest='; cat
} < test-stdio.in > test-stdio.out
grep "input=string1" test-stdio.out
grep "rest=string2" test-stdio.out

# Test with --run.
nbdkit -U - -v $plugin one=1 --run 'printf cmd=; cat' \
    < test-stdio.in > test-stdio.out
cat test-stdio.out
grep "one=string1" test-stdio.out
grep "cmd=string2" test-stdio.out

# Test with -f; we have to repeat body of start_nbdkit ourselves
echo "string" | nbdkit -P test-stdio.pid1 -v --filter=exitlast \
    -f -U $sock1 $plugin two=2 | tee test-stdio.out & pid=$!
for i in {1..60}; do
    if test -s test-stdio.pid1; then
        break
    fi
    sleep 1
done
if ! test -s test-stdio.pid1; then
    echo "$0: PID file $pidfile was not created"
    exit 1
fi
nbdsh -u "nbd+unix:///?socket=$sock1" -c 'buf = h.pread (512, 0)'
wait $pid
grep "two=string" test-stdio.out

# Test as daemon
echo "string" | start_nbdkit -P test-stdio.pid2 --filter=exitlast \
    -U $sock2 $plugin three=3 | tee test-stdio.out
nbdsh -u "nbd+unix:///?socket=$sock2" -c 'buf = h.pread (512, 0)'
grep "three=string" test-stdio.out

# Test with -s; here, the plugin produces no output.
nbdsh -c '
h.connect_command (["nbdkit", "-v", "-s", "'$plugin'", "four=4"])
buf = h.pread (512, 0)
'
