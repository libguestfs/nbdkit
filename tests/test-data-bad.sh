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

# Since this test is expected to fail, valgrind will also fail.
if [ "$NBDKIT_VALGRIND" = "1" ]; then
    echo "$0: skipping test under valgrind."
    exit 77
fi

bad ()
{
    data="$1"

    # This command is expected to fail.
    if nbdkit -U - -fv -D data.AST=1 data "$data" --run true; then
        echo "$0: data plugin was expected to fail on bad input: $data"
        exit 1
    fi
}

# Invalid bytes, numbers and words.
bad '-1'
bad '-'
bad '256'
bad '0400'
bad '0x100'
bad '0xfff'
bad '0xffffffffffffffff'
bad '0xffffffffffffffffffffffffffffffff'

for prefix in le16 be16 le32 be32 le64 be64; do
    bad "$prefix"
    bad "$prefix:"
    bad "$prefix:-1"
    bad "$prefix:abc"
    bad "$prefix:0xffffffffffffffffffffffffffffffff"
done

bad 'le16:0x10000'
bad 'be16:0x10000'
bad 'le32:0x100000000'
bad 'be32:0x100000000'
bad 'le64:0x10000000000000000'
bad 'be64:0x10000000000000000'

# Invalid barewords and symbols.
bad 'a'
bad 'x'
bad 'be'
bad 'le'
bad 'be1'
bad 'le3'
bad '¢'
#bad '0x'  # should fail but does not XXX
bad '?'
bad '\'
bad '^'
bad '@'
bad '@+'
bad '@-'
bad '@^'
bad '<'
bad '<('
bad '$'
bad '*'
bad '['
bad ':'
bad ']'

# Invalid offsets.
bad '@-2'
bad '@2 @-3'
bad '@2 @-2 @-1'
bad '@1 @^2 @-3'

# Mismatched parentheses.
bad "("
bad ")"
bad "( ("
bad "( ( )"
bad ") ( )"
bad "( ) )"

# Invalid strings.
bad '"'
bad '"\'
bad '"\\'
bad '"\"'
bad '"\x"'

# Bad repeats.
bad '*0'
bad '*1'
bad '0*-1'
bad '0*'
bad '0*x'
bad '0**'

# Bad slices.
bad '0[2:]'
bad '0[:2]'
bad '[:]'
bad '[0:1]'
bad '"111"[4:]'
bad '"123"[4:]'
bad '"123"[1:0]'

# Bad files.
if [ ! -r /NOFILE ]; then
    bad '</NOFILE'
fi

# Unknown and out of scope assignments.
bad '\a'
bad '( 0 -> \a ) \a'
bad '\a ( 0 -> \a )'
bad '0 -> \a \b'
bad '0 -> \a \a \b'
bad '( 0 -> \a \a ) \a'

# Currently this should fail.  \b subtitutes (0 -> \a) which only
# defines \a in the scope, not where it is used outside the scope.
# Should we make it work in future?
bad '(0 -> \a) -> \b \b \a'

# Unknown variable definition
unset unknownvar
bad '$unknownvar'
