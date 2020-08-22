#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018 Red Hat Inc.
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

# Test nbdkit -D option.

source ./functions.sh
set -x

requires_plugin example2

export LANG=C

files=debug-flags.out
rm -f $files
cleanup_fn rm -f $files

expected_failure ()
{
    echo "expected previous nbdkit command to fail, but it did not"
    exit 1
}

check_error ()
{
    cat debug-flags.out
    if ! grep -sq "$1" debug-flags.out; then
        echo "$0: expected error message not present in above output: '$1'"
        exit 1
    fi
}

check_warning ()
{
    cat debug-flags.out
    if ! grep -sq "warning.*$1" debug-flags.out; then
        echo "$0: expected warning message not present in above output: '$1'"
        exit 1
    fi
}

# This is expected to fail because we didn't set the file= parameter,
# but it should not fail because of the debug flag.
nbdkit -f -D example2.extra=1 example2 2>debug-flags.out && expected_failure
check_error "you must supply the file="

# This should fail because we didn't set the file= parameter, but it
# should also print a warning about the unknown -D flag.
nbdkit -f -D example2.unknown=1 example2 2>debug-flags.out && expected_failure
check_error "you must supply the file="
check_warning "does not contain a global variable called example2_debug_unknown"

# This should fail because we didn't set the file= parameter, but it
# should also print a warning because the -D flag is unused.
nbdkit -f -D example1.foo=1 example2 2>debug-flags.out && expected_failure
check_error "you must supply the file="
check_warning "was not used"

# These should fail because the -D flag has a bad format.
nbdkit -f -D = example2 2>debug-flags.out && expected_failure
check_error "must have the format"
nbdkit -f -D . example2 2>debug-flags.out && expected_failure
check_error "must have the format"
nbdkit -f -D =. example2 2>debug-flags.out && expected_failure
check_error "must have the format"
nbdkit -f -D .= example2 2>debug-flags.out && expected_failure
check_error "must have the format"
nbdkit -f -D .extra=1 example2 2>debug-flags.out && expected_failure
check_error "must have the format"
nbdkit -f -D example2.=1 example2 2>debug-flags.out && expected_failure
check_error "must have the format"
nbdkit -f -D example2.extra= example2 2>debug-flags.out && expected_failure
check_error "must have the format"
