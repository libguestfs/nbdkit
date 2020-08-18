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

# Is the readahead filter faster?  Copy a blank disk with a custom
# plugin that sleeps on every request.  Because the readahead filter
# should result in fewer requests it should run faster.

source ./functions.sh
set -e
set -x

requires_unix_domain_sockets
requires python3 --version
requires python3 -c 'import nbd'
requires dd iflag=count_bytes </dev/null

files="readahead.img"
rm -f $files
cleanup_fn rm -f $files

test ()
{
    start_t=$SECONDS
    nbdkit -fv -U - "$@" sh ./test-readahead-test-plugin.sh \
           --run './test-readahead-test-request.py $unixsocket'
    end_t=$SECONDS
    echo $((end_t - start_t))
}

t1=$(test --filter=readahead)
t2=$(test)

# In the t1 case we should make only 1 request into the plugin,
# resulting in around 1 sleep period (5 seconds).  In the t2 case we
# make 10 requests so sleep for around 50 seconds.  t1 should be < t2
# is every reasonable scenario.
if [ $t1 -ge $t2 ]; then
    echo "$0: readahead filter took longer, should be shorter"
    exit 1
fi
