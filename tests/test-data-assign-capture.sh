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

# Test that data plugin -> assignments capture their environment.

source ./functions.sh
set -e
set -x

requires nbdsh --version

sock=`mktemp -u`
files="data-assign-capture.pid $sock"
rm -f $files
cleanup_fn rm -f $files

# Run nbdkit.
start_nbdkit -P data-assign-capture.pid -U $sock \
       data '
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
'

nbdsh --connect "nbd+unix://?socket=$sock" \
      -c '
print ("%d" % h.get_size())
assert h.get_size() == 5
buf = h.pread (h.get_size(), 0)
print ("%r" % buf)
assert buf == b"\x01\x01\x01\x01\x04"
'
