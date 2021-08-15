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

# Test the data plugin data parameter.

source ./functions.sh
set -e
set -x

requires_run
requires_nbdsh_uri

# Recent valgrind deadlocks when running this test.
# XXX Temporarily disable it until valgrind is fixed.
# https://bugzilla.redhat.com/show_bug.cgi?id=1755400
if test -n "$NBDKIT_VALGRIND"; then
    echo "$0: test skipped because valgrind is broken"
    exit 77
fi

# Which allocators can we test?
allocators="sparse malloc"

if nbdkit data --dump-plugin | grep -sq zstd=yes; then
    allocators="$allocators zstd"
fi

# Compare the data parameter to expected output.  The second parameter
# (expected) is a literal snippet of Python.  It cannot contain single
# quote characters, but anything else is fine.
do_test ()
{
    data="$1"
    py="$2"
    shift 2

    for allocator in $allocators; do
        nbdkit -U - -v -D data.AST=1 \
               data "$data" \
               allocator=$allocator "$@" \
               --run '
                  nbdsh --uri "$uri" -c '\''expected='"$py"\'' -c "
size = h.get_size()
if size > 0:
    actual = h.pread(size, 0)
else:
    actual = b\"\"
def trunc(s): return s[:128] + (s[128:] and b\"...\")
print(\"actual:   %r\" % trunc(actual))
print(\"expected: %r\" % trunc(expected))
assert actual == expected
"'
    done
}

#----------------------------------------------------------------------
# The tests.

# Basic types.
do_test '' 'b""'
do_test '0' 'b"\x00"'
do_test '1' 'b"\x01"'
do_test '65' 'b"A"'
do_test '128' 'b"\x80"'
do_test '0xff' 'b"\xff"'
do_test '0x31' 'b"1"'
do_test '017' 'b"\x0f"'
do_test '0 0' 'b"\x00\x00"'
do_test '0 1' 'b"\x00\x01"'
do_test '1 0' 'b"\x01\x00"'
do_test '1 2' 'b"\x01\x02"'
do_test '1 0xcc' 'b"\x01\xcc"'

do_test '""' 'b""'
do_test '"foo"' 'b"foo"'
do_test '"foo\x00"' 'b"foo\x00"'
do_test '"\x00foo\x00"' 'b"\x00foo\x00"'
do_test ' "hello" ( "\"" "\\" )*2 "\x01\x02\x03" "\n" ' \
        'b"hello\"\\\"\\\x01\x02\x03\n"'

do_test '@4 "\x00"' 'b"\x00\x00\x00\x00\x00"'
do_test '@+4 "\x00"' 'b"\x00\x00\x00\x00\x00"'
do_test '@+4 @-1 "\x00"' 'b"\x00\x00\x00\x00"'
do_test '1 @^4 "\x00"' 'b"\x01\x00\x00\x00\x00"'

#----------------------------------------------------------------------
# Comments.
do_test '#' 'b""'
do_test '# ignore' 'b""'
do_test '# ignore
         1 2 3' 'b"\x01\x02\x03"'

#----------------------------------------------------------------------
# Nested expressions and repeat.

# Simple nested expression without any operator.
do_test '( 0x55 0xAA )' 'b"\x55\xAA"'

# Check that *1 is optimized correctly.
do_test '( 0x55 0xAA )*1' 'b"\x55\xAA"'

# Repeated nest.
do_test '( 0x55 0xAA )*4' 'b"\x55\xAA" * 4'

# Doubly-nested.
do_test '( @4 ( 0x21 )*4 )*4' 'b"\0\0\0\0!!!!"*4'

# Nest + offset alignment at the end.
do_test '( "Hello" @^8 )*2' 'b"Hello\0\0\0Hello\0\0\0"'

# Nest with only an offset.
do_test '( @4 )' 'bytearray(4)'
do_test '( @4 )*4 1' 'bytearray(16) + b"\x01"'
do_test '( @7 )*4' 'bytearray(28)'

# These should all expand to nothing.  They are here to test corner
# cases in the parser and optimizer.
do_test '
() ()*2 ( () ) ( ()*2 ) ( () () )*2
()*2[:0] ()[:0]*2 (()[:0]*2)[:0]*2
()*0 ( 1 2 3 )*0
()*1*2*3
() -> \a
\a (\a) (\a)*2 (\a \a) (\a*2 \a)
\a*2[:0] \a[:0]*2
' 'b""'

# In nbdkit <= 1.27.5 this caused allocator=zstd to crash.
do_test '0*10' 'bytearray(10)'

#----------------------------------------------------------------------
# Test various optimizations preserve the meaning.

# expr*X*Y is equivalent to expr*(X*Y)
do_test '1*2*3' 'b"\x01" * 6'
do_test '1*2*3*4' 'b"\x01" * 24'

# ( ( expr ) ) optimized to ( expr )
do_test '( ( ( ( 1 2 ) ) ) )' 'b"\x01\x02"'

# string*N is sometimes optimized to N copies of string.
do_test '"foo"*2' 'b"foo" * 2'
do_test '"foo"*2*2' 'b"foo" * 4'
do_test '"foo"*2*50' 'b"foo" * 100'

# ( const ) is sometimes optimized to const
do_test '( "foo" )' 'b"foo"'
do_test '( <(echo hello) )' 'b"hello\n"'
do_test '( $hello )' 'b"hello"' hello=' "hello" '

#----------------------------------------------------------------------
# Assignments.
do_test '
# Assign to \a in the outer scope.
(0x31 0x32) -> \a
(0x35 0x36) -> \b
(
  # Assign to \a and \c in the inner scope.
  (0x33 0x34) -> \a
  (0x37 0x38) -> \c
  \a \a \b \c
)
# The end of the inner scope should restore the outer
# scope definition of \a.
\a \b
' 'b"343456781256"'

# Test environment capture in -> assignments.
do_test '
# In the global scope, assign \1 = 1
1 -> \1

# Create a few more assignments.  These should make no difference
# to anything.
"foo" -> \foo
42 -> \a-number
2 -> \2

# Capture the assignment to \1 (= 1) in a nested expression.
# Note this is parsed as EXPR_ASSIGN ("\test", EXPR_EXPR (EXPR_NAME "\1"))
( \1 ) -> \test

# Obviously this should evaluate to 1.
\test

# Since assignments only last to the end of the scope in which they
# are created, the assignment inside the nested scope here should make
# no difference, \test should still evaluate to 1.
( 2 -> \1 )  \test

# In a nested scope, change \1 and evaluate \test.
# It should still be 1 because of the captured environment from above.
( 3 -> \1  \test )

# In the global scope, change \1 and evaluate \test.
# It should still be 1 because of the captured environment from above.
4 -> \1  \test

# Now reassign \test to the new value of \1 (= 4) and evaluate.
# It should now be the new value (4).
( \1 ) -> \test
\test
' 'b"\x01\x01\x01\x01\x04"'

#----------------------------------------------------------------------
# Slices.
do_test '( $hello )[:4]' 'b"Hell"' hello=' "Hello" '
do_test '( "Hello" )[3:]' 'b"lo"'
do_test '( "Hello" )[3:5]' 'b"lo"'

# With the new parser it should work without the parens too.
do_test '"Hello"[:]' 'b"Hello"'
do_test '$hello[:]' 'b"Hello"' hello=' "Hello" '
do_test '$hello[:4]' 'b"Hell"' hello=' "Hello" '
do_test '"Hello"[3:]' 'b"lo"'
do_test '"Hello"[3:5]' 'b"lo"'

# Zero length slices are optimized out.  The first index is ignored.
do_test '"Hello"[:0]' 'b""'
do_test '"Hello"[99:99]' 'b""'

#----------------------------------------------------------------------
# Scripts.
do_test '<( for i in `seq 0 7`; do printf "%04d" $i; done )' \
        'b"00000001000200030004000500060007"'
do_test '<( i=0
                while :; do
                    printf "%04d" $((i++))
                done )[:65]' \
        'b"00000001000200030004000500060007000800090010001100120013001400150"'

#----------------------------------------------------------------------
# Variables.

# Check there are no environment variables that might
# interfere with the test.
unset a
unset b
unset c

# Unknown variable should fail.
if nbdkit -U - data ' $a $b $c ' --run 'exit 0'; then
    echo "$0: expected unknown variables to fail"
    exit 1
fi

# Set environment variables to patterns.
export a=' 1 2 '
export b=' 3 4 '
export c=' 5*5 '
do_test ' $a $b $c ' 'b"\x01\x02\x03\x04\x05\x05\x05\x05\x05"'

# Same but using command line parameters.
unset a
unset b
unset c
do_test ' $a $b $c ' 'b"\x01\x02\x03\x04\x05\x05\x05\x05\x05"' \
        a=' 1 2 ' b=' 3 4 ' c=' 5*5 '

# Same but using a mix.
export a='BLAH'
export b=' 3 4 '
export c='FOO'
do_test ' $a $b $c ' 'b"\x01\x02\x03\x04\x05\x05\x05\x05\x05"' \
        a=' 1 2 ' c=' 5*5 '

# Variables referencing variables.
unset a
unset b
unset c
do_test ' $a $b $c ' 'b"\x01\x02\x03\x04\x05\x05\x05\x05\x05"' \
        a=' 1 2 ' b=' 3 4 ' c=' $d*5 ' d=' 5 '

# Badly formatted variable should fail.
if nbdkit -U - data ' $a ' a='NONSENSE' --run 'exit 0'; then
    echo "$0: expected unknown variables to fail"
    exit 1
fi

# Using an extra parameter without data= should fail.
if nbdkit -U - data raw='' a='NONSENSE' --run 'exit 0'; then
    echo "$0: expected extra params to fail with !data"
    exit 1
fi

unset a
unset b
unset c

#----------------------------------------------------------------------
# Some tests at offsets.
#
# Most of the tests above fit into a single page for sparse and zstd
# allocators (32K).  It could be useful to test at page boundaries.

do_test '@32766 1 2 3'   'b"\0"*32766 + b"\x01\x02\x03"'
do_test '@32766 1*6'     'b"\0"*32766 + b"\x01"*6'
do_test '@32766 1*32800' 'b"\0"*32766 + b"\x01"*32800'

# Since we do sparseness detection, automatically trimming whole
# pages if they are zero, this should be interesting:
do_test '@32766 1*5 @65534 2*5 @32768 0*32768' \
        'b"\0"*32766 + b"\x01\x01" + b"\0"*32768 + b"\x02\x02\x02"'
