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

source ./functions.sh
set -e
set -x

requires hexdump --version
requires $PYTHON --version
requires_nbdcopy
requires_plugin python

# Python has proven very difficult to valgrind, therefore it is disabled.
if [ "$NBDKIT_VALGRIND" = "1" ]; then
    echo "$0: skipping Python test under valgrind."
    exit 77
fi

# There is a fake boto3 module in test-S3/ which we use as a test
# harness for the plugin.
requires test -d test-S3
export PYTHONPATH=$srcdir/test-S3:$PYTHONPATH

file=S3.out
rm -f $file
cleanup_fn rm -f $file

# The fake module checks the parameters have these particular values.
nbdkit -U - S3 \
       access-key=TEST_ACCESS_KEY \
       secret-key=TEST_SECRET_KEY \
       session-token=TEST_SESSION_TOKEN \
       endpoint-url=TEST_ENDPOINT \
       bucket=MY_FILES \
       key=MY_KEY \
       --run "nbdcopy \"\$uri\" $file"

ls -l $file
hexdump -C $file

if [ "$(hexdump -C $file)" != "00000000  78 78 78 78 78 78 78 78  78 78 78 78 78 78 78 78  |xxxxxxxxxxxxxxxx|
*
00001000  79 79 79 79 79 79 79 79  79 79 79 79 79 79 79 79  |yyyyyyyyyyyyyyyy|
*
00001800  7a 7a 7a 7a 7a 7a 7a 7a  7a 7a 7a 7a 7a 7a 7a 7a  |zzzzzzzzzzzzzzzz|
*
00002000" ]; then
    echo "$0: unexpected output from test"
    exit 1
fi
