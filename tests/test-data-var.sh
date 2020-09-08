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

# Test the data plugin with $var.

source ./functions.sh
set -e
set -x

requires nbdcopy --version
requires hexdump --version

out=test-data-var.out
rm -f $out
cleanup_fn rm -f $out

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
a=' 1 2 ' b=' 3 4 ' c=' 5*5 ' \
nbdkit -U - data ' $a $b $c ' \
       --run 'nbdcopy "$uri" - | hexdump -C' > $out
cat $out
grep "00000000  01 02 03 04 05 05 05 05  05" $out

# Same but using command line parameters.
nbdkit -U - data ' $a $b $c ' \
       a=' 1 2 ' b=' 3 4 ' c=' 5*5 ' \
       --run 'nbdcopy "$uri" - | hexdump -C' > $out
cat $out
grep "00000000  01 02 03 04 05 05 05 05  05" $out

# Same but using a mix.
a='BLAH' b=' 3 4 ' c='FOO' \
nbdkit -U - data ' $a $b $c ' \
       a=' 1 2 ' c=' 5*5 ' \
       --run 'nbdcopy "$uri" - | hexdump -C' > $out
cat $out
grep "00000000  01 02 03 04 05 05 05 05  05" $out

# Variables referencing variables.
nbdkit -U - data ' $a $b $c ' \
       a=' 1 2 ' b=' 3 4 ' c=' $d*5 ' d=' 5 ' \
       --run 'nbdcopy "$uri" - | hexdump -C' > $out
cat $out
grep "00000000  01 02 03 04 05 05 05 05  05" $out

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
