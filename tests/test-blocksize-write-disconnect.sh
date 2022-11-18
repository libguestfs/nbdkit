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

# Libnbd does not let us test pwrite larger than 64M, so we can't
# test nbdkit's graceful behavior of writes up to 128M.
# In this test, odd size writes fail with EINVAL from the filter (size 1 too
# small, all others unaligned); evens 2 to 8M pass, 8M+2 to 16M fail with
# ENOMEM from the plugin, 16M+2 to 32M fail with EINVAL from the filter,
# 32M+1 to 64M kill the connection (ENOTCONN visible to client), and
# 64M+1 and above fails with ERANGE in libnbd.

nbdkit -v -U - eval \
       block_size="echo 2 4096 16M" \
       get_size="echo 64M" \
       pread=' dd if=/dev/zero count=$3 iflag=count_bytes ' \
       pwrite=' if test $3 -gt $((8*1024*1024)); then
           echo ENOMEM >&2; exit 1
         else
           dd of=/dev/null
         fi' \
       --filter=blocksize-policy \
       blocksize-error-policy=error blocksize-write-disconnect=32M \
       --run '
nbdsh -u "$uri" -c "
import errno
import sys

def header(msg):
  print(\"=== %s ===\" % msg)
  sys.stdout.flush()

def check(h, size, expect_value, expect_traffic=True):
  print(\"Testing size=%d\" % size)
  sys.stdout.flush()

  assert h.aio_is_ready() is True

  buf = b\"0\" * size
  if hasattr(h, \"stats_bytes_sent\"):
    start = h.stats_bytes_sent()
  try:
    h.pwrite(buf, 0)
    assert expect_value == 0
  except nbd.Error as ex:
    assert expect_value == ex.errnum
  if hasattr(h, \"stats_bytes_sent\"):
    if expect_traffic:
      assert h.stats_bytes_sent() > start
    else:
      assert h.stats_bytes_sent() == start

h.set_strict_mode(0)  # Bypass client-side safety checks
header(\"Beyond 64M\")
check(h, 64*1024*1024 + 1, errno.ERANGE, False)
check(h, 64*1024*1024 + 2, errno.ERANGE, False)
header(\"Small reads\")
check(h, 1, errno.EINVAL)
check(h, 2, 0)
header(\"Near 8M boundary\")
check(h, 8*1024*1024 - 2, 0)
check(h, 8*1024*1024 - 1, errno.EINVAL)
check(h, 8*1024*1024, 0)
check(h, 8*1024*1024 + 1, errno.EINVAL)
check(h, 8*1024*1024 + 2, errno.ENOMEM)
header(\"Near 16M boundary\")
check(h, 16*1024*1024 - 2, errno.ENOMEM)
check(h, 16*1024*1024 - 1, errno.EINVAL)
check(h, 16*1024*1024, errno.ENOMEM)
check(h, 16*1024*1024 + 1, errno.EINVAL)
check(h, 16*1024*1024 + 2, errno.EINVAL)
header(\"Near 32M boundary\")
check(h, 32*1024*1024 - 2, errno.EINVAL)
check(h, 32*1024*1024 - 1, errno.EINVAL)
check(h, 32*1024*1024, errno.EINVAL)
check(h, 32*1024*1024 + 1, errno.ENOTCONN)
assert h.aio_is_ready() is False
"'
