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
requires_nbdsh_uri

# Run nbdkit eval + filter and check resulting block size constraints
# using some nbdsh.

# No parameters.
nbdkit -U - eval \
       block_size="echo 64K 128K 32M" \
       get_size="echo 0" \
       --filter=blocksize-policy \
       --run 'nbdsh \
           -u "$uri" \
           -c "assert h.get_block_size(nbd.SIZE_MINIMUM) == 64 * 1024" \
           -c "assert h.get_block_size(nbd.SIZE_PREFERRED) == 128 * 1024" \
           -c "assert h.get_block_size(nbd.SIZE_MAXIMUM) == 32 * 1024 * 1024" \
      '

# Adjust single values.
nbdkit -U - eval \
       block_size="echo 64K 128K 32M" \
       get_size="echo 0" \
       --filter=blocksize-policy \
       blocksize-minimum=1 \
       --run 'nbdsh \
           -u "$uri" \
           -c "assert h.get_block_size(nbd.SIZE_MINIMUM) == 1" \
           -c "assert h.get_block_size(nbd.SIZE_PREFERRED) == 128 * 1024" \
           -c "assert h.get_block_size(nbd.SIZE_MAXIMUM) == 32 * 1024 * 1024" \
      '
nbdkit -U - eval \
       block_size="echo 64K 128K 32M" \
       get_size="echo 0" \
       --filter=blocksize-policy \
       blocksize-preferred=64K \
       --run 'nbdsh \
           -u "$uri" \
           -c "assert h.get_block_size(nbd.SIZE_MINIMUM) == 64 * 1024" \
           -c "assert h.get_block_size(nbd.SIZE_PREFERRED) == 64 * 1024" \
           -c "assert h.get_block_size(nbd.SIZE_MAXIMUM) == 32 * 1024 * 1024" \
      '
nbdkit -U - eval \
       block_size="echo 64K 128K 32M" \
       get_size="echo 0" \
       --filter=blocksize-policy \
       blocksize-maximum=1M \
       --run 'nbdsh \
           -u "$uri" \
           -c "assert h.get_block_size(nbd.SIZE_MINIMUM) == 64 * 1024" \
           -c "assert h.get_block_size(nbd.SIZE_PREFERRED) == 128 * 1024" \
           -c "assert h.get_block_size(nbd.SIZE_MAXIMUM) == 1 * 1024 * 1024" \
      '

# Adjust all values for a plugin which is advertising.
nbdkit -U - eval \
       block_size="echo 64K 128K 32M" \
       get_size="echo 0" \
       --filter=blocksize-policy \
       blocksize-minimum=1 \
       blocksize-preferred=4K \
       blocksize-maximum=1M \
       --run 'nbdsh \
           -u "$uri" \
           -c "assert h.get_block_size(nbd.SIZE_MINIMUM) == 1" \
           -c "assert h.get_block_size(nbd.SIZE_PREFERRED) == 4 * 1024" \
           -c "assert h.get_block_size(nbd.SIZE_MAXIMUM) == 1 * 1024 * 1024" \
      '

# Set all values for a plugin which is not advertising.
nbdkit -U - eval \
       get_size="echo 0" \
       --filter=blocksize-policy \
       blocksize-minimum=1 \
       blocksize-preferred=4K \
       blocksize-maximum=1M \
       --run 'nbdsh \
           -u "$uri" \
           -c "assert h.get_block_size(nbd.SIZE_MINIMUM) == 1" \
           -c "assert h.get_block_size(nbd.SIZE_PREFERRED) == 4 * 1024" \
           -c "assert h.get_block_size(nbd.SIZE_MAXIMUM) == 1 * 1024 * 1024" \
      '
