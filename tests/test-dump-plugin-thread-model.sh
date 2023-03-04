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

# Test that --dump-plugin can be used to introspect a resulting
# dynamic thread model.

source ./functions.sh
set -e
set -x

requires_plugin file
requires_plugin sh
requires_filter noparallel

# First, get a baseline (since a system without atomic CLOEXEC can't
# do parallel). Then test various patterns with the sh plugin.
max=$(nbdkit --dump-plugin file | $SED -n '/^thread_model=/ s///p')

# With no script, thread_model matches the baseline
out=$(nbdkit --dump-plugin sh | grep thread_model)
exp="max_thread_model=parallel
thread_model=$max
has_thread_model=1"
if [ "$out" != "$exp" ]; then
    echo "thread_model mismatch"; exit 1
fi

# With a script that does not specify a model, historical back-compat
# forces the model to serialized
out=$(nbdkit --dump-plugin sh - <<\EOF | grep ^thread_model
case $1 in
    get_size) echo 1M ;;
    *) exit 2 ;;
esac
EOF
)
if [ "$out" != "thread_model=serialize_all_requests" ]; then
    echo "thread_model mismatch"; exit 1
fi

# A script can request the maximum, but will only get the baseline
out=$(nbdkit --dump-plugin sh - <<\EOF | grep ^thread_model
case $1 in
    thread_model) echo parallel ;;
    get_size) echo 1M ;;
    *) exit 2 ;;
esac
EOF
)
if [ "$out" != "thread_model=$max" ]; then
    echo "thread_model mismatch"; exit 1
fi

# A script can request an even stricter model
out=$(nbdkit --dump-plugin sh - <<\EOF | grep ^thread_model
case $1 in
    thread_model) echo serialize_connections ;;
    get_size) echo 1M ;;
    *) exit 2 ;;
esac
EOF
)
if [ "$out" != "thread_model=serialize_connections" ]; then
    echo "thread_model mismatch"; exit 1
fi

# Finally, a filter can restrict things
out=$(nbdkit --dump-plugin --filter=noparallel sh - \
	     serialize=connections <<\EOF | grep ^thread_model
case $1 in
    get_size) echo 1M ;;
    *) exit 2 ;;
esac
EOF
)
if [ "$out" != "thread_model=serialize_connections" ]; then
    echo "thread_model mismatch"; exit 1
fi
