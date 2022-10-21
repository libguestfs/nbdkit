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

# Test size of data expression is calculated correctly.

source ./functions.sh
set -e
set -x

requires_run
requires_nbdinfo

# Since this test is expected to fail, valgrind will also fail.
if [ "$NBDKIT_VALGRIND" = "1" ]; then
    echo "$0: skipping test under valgrind."
    exit 77
fi

size ()
{
    data="$1"
    expected_size="$2"

    size="$(
      nbdkit -U - -fv -D data.AST=1 data "$data" --run 'nbdinfo --size "$uri"'
    )"
    if [ "$size" -ne "$expected_size" ]; then
        echo "$0: data has unexpected size: $data"
        echo "$0: expected size:  $expected_size"
        echo "$0: actual size:    $size"
        exit 1
    fi
}

# Some obvious expressions.
size '' 0
size '0' 1
size '0 0' 2
size '0 1 2 3 0' 5
size '(0 1 2 3 0)' 5
size 'le16:0' 2
size 'be16:0' 2
size 'le32:0' 4
size 'be32:0' 4
size 'le64:0' 8
size 'be64:0' 8

# @offsets should extend the size even if nothing appears later.
size '@100' 100
size '@0' 0
size '@+100' 100
size '@+100 @-90' 100

# Nested expressions involving offsets.
size '(@100) (@50)' 150
size '(0 1 2 3 @100) (4 5 6 @50)' 150
size '(@100) @0 (@50)' 100
size '(@50) @0 (@100)' 100
size '(@50) @200 (@100)' 300

# Alignment expressions.
size '0 @^512' 512
size '0 @^512 0' 513

# Alignment expressions in assignments.
#size '@100 -> \a \a' 100    # This probably should work but doesn't
size '(0 @100) -> \a \a' 100
size '(0 @100) -> \a (0 @200) -> \b \a \b' 300
size '(0 @100) -> \a (0 @200) -> \b \a @0 \b' 200

# Scripts.
size '<( for i in `seq 0 99`; do printf "%04d" $i; done )' $(( 100 * 4 ))
size '<( while true; do echo -n .; done )[:400]' 400

# Strings.
size '""' 0
size '"\\"' 1
size '"\""' 1
size '"abc"' 3

# Comments.
size '# nothing' 0
