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

requires_plugin sh
requires_nbdsh_uri
requires dd iflag=count_bytes </dev/null

files="readahead.out"
rm -f $files
cleanup_fn rm -f $files

nbdkit -fv -U - "$@" sh - \
       --filter=readahead \
       --run 'nbdsh --uri "$uri" -c "
for i in range(0, 512*10, 512):
    h.pread(512, i)
"' <<'EOF'
case "$1" in
     thread_model)
         echo parallel
         ;;
     can_cache)
         echo native
         ;;
     get_size)
         echo 1M
         ;;
     cache)
         echo "$@" >> readahead.out
         ;;
     pread)
         echo "$@" >> readahead.out
         dd if=/dev/zero count=$3 iflag=count_bytes
         ;;
     *)
         exit 2
         ;;
esac
EOF

cat readahead.out

# We should see the pread requests, and additional cache requests for
# the 32K region following each pread request.
for i in `seq 0 512 $((512*10 - 512))` ; do
    grep "pread  512 $i" readahead.out
    grep "cache  32768 $((i+512))" readahead.out
done
