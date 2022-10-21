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

requires_run
requires_nbdinfo
requires_filter log

if ! nbdinfo --help | grep -- --map ; then
    echo "$0: nbdinfo --map option required to run this test"
    exit 77
fi

log=log-script-info.log
cleanup_fn rm -f $log
rm -f $log

nbdkit -U - --filter=log data "@32768 1" size=64K \
       logscript='
           if [ "$act" = "Extents" -a "$type" = "LEAVE" ]; then
               echo $act $type >>log-script-info.log
               # Print the extents, one per line.
               len=${#extents[@]}
               j=1
               for i in `seq 0 3 $((len-1))`; do
                   echo $j: ${extents[$i]} ${extents[$i+1]} ${extents[$i+2]} >>log-script-info.log
                   ((j++))
               done
           fi
       ' \
       --run 'nbdinfo --map "$uri" >/dev/null'

# Print the full log to help with debugging.
cat $log

# Check the extents are as expected.
grep "1: 0x0 0x8000 hole,zero" $log
grep "2: 0x8000 0x8000" $log
