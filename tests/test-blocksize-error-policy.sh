#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2019-2022 Red Hat Inc.
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

requires_plugin eval
requires nbdsh -c 'print(h.get_block_size)'
requires nbdsh -c 'print(h.get_strict_mode)'
requires_nbdsh_uri
requires dd iflag=count_bytes </dev/null

nbdkit -v -U - eval \
       block_size="echo 512 4096 1M" \
       get_size="echo 64M" \
       pread=" dd if=/dev/zero count=\$3 iflag=count_bytes " \
       --filter=blocksize-policy \
       blocksize-error-policy=error \
       --run '
nbdsh \
    -u "$uri" \
    -c "assert h.get_block_size(nbd.SIZE_MINIMUM) == 512" \
    -c "assert h.get_block_size(nbd.SIZE_PREFERRED) == 4096" \
    -c "assert h.get_block_size(nbd.SIZE_MAXIMUM) == 1024 * 1024" \
    -c "h.set_strict_mode(h.get_strict_mode() & ~nbd.STRICT_ALIGN)" \
    -c "
# These requests should work
b = h.pread(512, 0)
b = h.pread(512, 4096)
b = h.pread(1024*1024, 0)
" \
    -c "
# Count not a multiple of minimum size
try:
    h.pread(768, 0)
    assert False
except nbd.Error as ex:
    assert ex.errno == \"EINVAL\"
" \
    -c "
# Offset not a multiple of minimum size
try:
    h.pread(512, 768)
    assert False
except nbd.Error as ex:
    assert ex.errno == \"EINVAL\"
" \
    -c "
# Count smaller than minimum size
try:
    h.pread(256, 0)
    assert False
except nbd.Error as ex:
    assert ex.errno == \"EINVAL\"
" \
    -c "
# Count larger than maximum size
try:
    h.pread(2*1024*1024, 0)
    assert False
except nbd.Error as ex:
    assert ex.errno == \"EINVAL\"
"
'
