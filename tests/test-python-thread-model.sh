#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018-2020 Red Hat Inc.
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

SCRIPT="$SRCDIR/python-thread-model.py"
if ! test -d "$SRCDIR" || ! test -f "$SCRIPT"; then
    echo "$0: could not locate python-thread-model.py"
    exit 1
fi

# Python has proven very difficult to valgrind, therefore it is disabled.
if [ "$NBDKIT_VALGRIND" = "1" ]; then
    echo "$0: skipping Python test under valgrind."
    exit 77
fi

requires nbdsh --version

out=test-python-thread-model.out
pid=test-python-thread-model.pid
sock=`mktemp -u`
files="$out $pid $sock"
rm -f $files
cleanup_fn rm -f $files

# Check the plugin is loadable and the effective thread model is parallel.
nbdkit python $SCRIPT --dump-plugin >$out
grep "^thread_model=parallel" $out

start_nbdkit -P $pid -U $sock python $SCRIPT

export sock
nbdsh -c '
import os
import time

h.connect_unix(os.environ["sock"])

# We should be able to issue multiple requests in parallel,
# and the total time taken should not be much more than 10 seconds
# because all sleeps in the plugin should happen in parallel.
start_t = time.time()
for i in range(10):
    buf = nbd.Buffer(512)
    h.aio_pread(buf, 0)

while h.aio_in_flight() > 0:
    h.poll(-1)
end_t = time.time()

t = end_t - start_t
print(t)

# Since we launched 10 requests, if we serialized on them we
# would have waited at least 100 seconds.  We would expect to
# wait around 10 seconds, but for flexibility on slow servers
# any test < 100 should be fine.
assert t <= 50
'
