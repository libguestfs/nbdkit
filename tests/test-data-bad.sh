#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018-2021 Red Hat Inc.
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

# Test bad input to the data plugin.

source ./functions.sh
set -e
set -x

requires_run
requires_nbdsh_uri

# Since this test is expected to fail, valgrind will also fail.
if [ "$NBDKIT_VALGRIND" = "1" ]; then
    echo "$0: skipping test under valgrind."
    exit 77
fi

do_test ()
{
    data="$1"

    # This command is expected to fail.
    if nbdkit -U - -fv -D data.AST=1 data "$data" --run true; then
        echo "$0: data plugin was expected to fail on bad input: $data"
        exit 1
    fi
}

# Invalid bytes, numbers and words.
do_test '-1'
do_test '-'
do_test '256'
do_test '0400'
do_test '0x100'
do_test '0xfff'
do_test '0xffffffffffffffff'
do_test '0xffffffffffffffffffffffffffffffff'

for prefix in le16 be16 le32 be32 le64 be64; do
    do_test "$prefix"
    do_test "$prefix:"
    do_test "$prefix:-1"
    do_test "$prefix:abc"
    do_test "$prefix:0xffffffffffffffffffffffffffffffff"
done

do_test 'le16:0x10000'
do_test 'be16:0x10000'
do_test 'le32:0x100000000'
do_test 'be32:0x100000000'
do_test 'le64:0x10000000000000000'
do_test 'be64:0x10000000000000000'

# Invalid barewords and symbols.
do_test 'a'
do_test 'x'
do_test 'be'
do_test 'le'
do_test 'be1'
do_test 'le3'
do_test '¢'
#do_test '0x'  # should fail but does not XXX
do_test '?'
do_test '\'
do_test '^'
do_test '@'
do_test '@+'
do_test '@-'
do_test '@^'
do_test '<'
do_test '<('
do_test '$'
do_test '*'
do_test '['
do_test ':'
do_test ']'

# Invalid offsets.
do_test '@-2'
do_test '@2 @-3'
do_test '@2 @-2 @-1'
do_test '@1 @^2 @-3'

# Mismatched parentheses.
do_test "("
do_test ")"
do_test "( ("
do_test "( ( )"
do_test ") ( )"
do_test "( ) )"

# Invalid strings.
do_test '"'
do_test '"\'
do_test '"\\'
do_test '"\"'
do_test '"\x"'

# Bad repeats.
do_test '*0'
do_test '*1'
do_test '0*-1'
do_test '0*'
do_test '0*x'
do_test '0**'

# Bad slices.
do_test '0[2:]'
do_test '0[:2]'
do_test '[:]'
do_test '[0:1]'
do_test '"111"[4:]'
do_test '"123"[4:]'
do_test '"123"[1:0]'

# Bad files.
if [ ! -r /NOFILE ]; then
    do_test '</NOFILE'
fi

# Unknown and out of scope assignments.
do_test '\a'
do_test '( 0 -> \a ) \a'
do_test '\a ( 0 -> \a )'
do_test '0 -> \a \b'
do_test '0 -> \a \a \b'
do_test '( 0 -> \a \a ) \a'

# Unknown variable definition
unset unknownvar
do_test '$unknownvar'
