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

source ./functions.sh
set -e
set -x

files="dump-plugin.out dump-plugin.err"
rm -f $files
cleanup_fn rm -f $files

# Basic check that the name field is present.
output="$(nbdkit example1 --dump-plugin)"
if [[ ! ( "$output" =~ name\=example1 ) ]]; then
    echo "$0: unexpected output from nbdkit example1 --dump-plugin"
    echo "$output"
    exit 1
fi

# example2 overrides the --dump-plugin with extra data.
output="$(nbdkit example2 --dump-plugin)"
if [[ ! ( "$output" =~ example2_extra\=hello ) ]]; then
    echo "$0: unexpected output from nbdkit example2 --dump-plugin"
    echo "$output"
    exit 1
fi

# Run nbdkit plugin --dump-plugin for each plugin.
# However some of these tests are expected to fail.
do_test ()
{
    vg=; [ "$NBDKIT_VALGRIND" = "1" ] && vg="-valgrind"
    case "$1$vg" in
        vddk | vddk-valgrind)
            echo "$0: skipping $1$vg because VDDK cannot run without special environment variables"
            ;;
        python-valgrind | ruby-valgrind | tcl-valgrind)
            echo "$0: skipping $1$vg because this language doesn't support valgrind"
            ;;
        *)
            nbdkit $1 --dump-plugin
            ;;
    esac
}
foreach_plugin do_test

# --dump-plugin and -s are incompatible
if nbdkit --dump-plugin -s null > dump-plugin.out 2> dump-plugin.err; then
    echo "$0: unexpected success from nbdkit -s --dump-plugin"
    echo "out:"
    cat dump-plugin.out
    echo "err:"
    cat dump-plugin.err
    exit 1
fi
if test -s dump-plugin.out; then
    echo "$0: unexpected output during nbdkit -s --dump-plugin"
    echo "out:"
    cat dump-plugin.out
    echo "err:"
    cat dump-plugin.err
    exit 1
fi
if test ! -s dump-plugin.err; then
    echo "$0: missing error message during nbdkit -s --dump-plugin"
    ecit 1
fi

# Test that --dump-plugin can be used to introspect a resulting dynamic
# thread model.  First, get a baseline (since a system without atomic
# CLOEXEC can't do parallel). Then test various patterns with the sh plugin.
max=$(nbdkit --dump-plugin file | sed -n '/^thread_model=/ s///p')

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
