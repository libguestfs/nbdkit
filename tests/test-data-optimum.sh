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

# Test optimizations of the data parameter happen.

source ./functions.sh
set -e

requires_run
requires_nbdsh_uri
requires $SED --version
requires tr --version

log=data-optimum.log
cleanup_fn rm -f $log
rm -f $log

# Run nbdkit-data-plugin with -D data.AST=1 (to print the final AST)
# and check that the expected AST (second parameter) appears in the
# debug output.
do_test ()
{
    data="$1"
    expected_AST="$2"

    nbdkit -U - -fv -D data.AST=1 data "$data" --run true >$log 2>&1

    # Collect up all lines of debug output containing the AST
    # and concatenate them into a single string.
    actual_AST="$(
        $SED -n '
        /BEGIN AST/,/END AST/{
            /BEGIN AST/n;
            /END AST/q;
            s/.*debug: //p;
        }' $log | tr -s '[:space:]' ' '
    )"
    actual_AST="${actual_AST% }" ;# remove trailing space

    # Check expected = actual AST
    if [ "$expected_AST" != "$actual_AST" ]; then
        echo "$0: '$data' was not optimized to expected abstract syntax tree"
        echo "expected AST = $expected_AST"
        echo "actual AST   = $actual_AST"
        echo "full output from nbdkit:"
        cat $log
        exit 1
    fi
}

# Single byte.
do_test '1' '1'

# Sequences of identical bytes are converted to fills.
do_test '1 1 1' 'fill(1*3)'

# Sequences of non-identical bytes are converted to strings.
do_test '0x31 0x32 0x33' '"123"'

# Strings of the same byte are converted to fills.
do_test '"1111" 0x31' 'fill(49*5)'

# Fills and bytes.
do_test '"111" 0x31' 'fill(49*4)'
do_test '0x31 "111"' 'fill(49*4)'

# Adjacent elements of lists are combined.
do_test '"12" "3" "456"' '"123456"'
do_test '"12" 0x33 "456"' '"123456"'

# Multiple nesting is eliminated.
do_test '((((1))))' '1'

# Adjacent lists are flattened and optimized.
do_test '(1 1 1) 1 (1 1 1 1)' 'fill(1*8)'
do_test '(((1 1 1))) 1 (1 1 1 1)' 'fill(1*8)'
do_test '(((1 1 1) 1)) (1 1 1 1)' 'fill(1*8)'

# Adjacent lists with non-identical bytes.
do_test '(((2 2 2) 2)) (1 1 1 1)' '( fill(2*4) fill(1*4) )'
do_test '(((2 2 2) 2)) (1 1 1 1 3)' '( fill(2*4) fill(1*4) 3 )'
# XXX could optimize these better:
do_test '(((2 2 2) 2)) (1 1 1 (1 3))' '( fill(2*4) fill(1*3) "\x01\x03" )'
do_test '(((2 2 2) 2)) (3 1 1 1 1)' '( fill(2*4) "\x03\x01\x01\x01\x01" )'

# Zero repeats become null.
do_test '(1 2 3)*0' 'null'

# X*Y repeats.
do_test '(0x31 0x32 0x33)*4*4' \
        '"123123123123123123123123123123123123123123123123"'
do_test '((0x31 0x32 0x33)*4)*4' \
        '"123123123123123123123123123123123123123123123123"'

# Slices of strings.
do_test '"123456"[:]'   '"123456"'
do_test '"123456"[0:6]' '"123456"'
do_test '"123456"[0:]'  '"123456"'
do_test '"123456"[1:]'  '"23456"'
do_test '"123456"[2:]'  '"3456"'
do_test '"123456"[:0]'  'null'
do_test '"123456"[:1]'  '49'
do_test '"123456"[:2]'  '"12"'
do_test '"123456"[1:2]' '50'
do_test '"123456"[2:4]' '"34"'
do_test '"123456"[2:2]' 'null'
do_test '"123456"[2:6]' '"3456"'
do_test '"123456"[5:]'  '54'
do_test '""[:]' 'null'

# Slices of fills.
do_test '(1 1 1 1 1 1)[:]'   'fill(1*6)'
do_test '(1 1 1 1 1 1)[0:6]' 'fill(1*6)'
do_test '(1 1 1 1 1 1)[1:6]' 'fill(1*5)'
do_test '(1 1 1 1 1 1)[2:3]' '1'
do_test '(1 1 1 1 1 1)[5:]'  '1'
do_test '(1 1 1 1 1 1)[6:6]' 'null'

# Invalid slice can be optimized away.
do_test '()[6:]*0' 'null'
