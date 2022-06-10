#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2022 Red Hat Inc.
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

# Check various .cache scenarios using eval

source ./functions.sh
set -e
set -x

requires_plugin eval
requires_nbdsh_uri

files="eval-cache.witness eval-cache.cache"
rm -f $files
cleanup_fn rm -f $files

# This plugin requests nbdkit to emulate caching with pread. When the witness
# file exists, cache reads; when absent, reads fail if not already cached.
export witness="$PWD/eval-cache.witness"
export cache="$PWD/eval-cache.cache"
export script='
import os
import errno

witness = os.getenv("witness")

def touch(path):
    open(path, "a").close()

# Test that uncached read fails
try:
    h.pread(1024 * 1024, 0)
except nbd.Error as ex:
    assert ex.errnum == errno.EIO

# Cache the entire image; nbdkit should break it into 64M preads
touch(witness)
h.cache(h.get_size(), 0)
os.unlink(witness)

# Now read should succeed
buf = h.pread(64 * 1024 * 1024, 64 * 1024 * 1024)
if hasattr(buf, "is_zero"):
    assert buf.is_zero()
'
nbdkit -U - -v eval \
    get_size='echo 128M' can_cache='echo emulate' open='touch "$cache"' \
    pread='
      if test -f "$witness"; then
        echo "$3 $4" >> "$cache"
      elif ! grep -q "^$3 $4$" "$cache"; then
        echo EIO >&2; exit 1
      fi
      dd if=/dev/zero count=$3 iflag=count_bytes
    ' --run 'nbdsh -u "$uri" -c "$script"'

# This plugin provides .cache but not .can_cache; eval should synthesize one.
nbdkit -U - -v eval \
    get_size='echo 1M' cache='exit 0' pread='echo EIO >&2; exit 1' \
    --run 'nbdsh -u "$uri" -c "assert h.can_cache()" \
      -c "h.cache(1024*1024, 0)"'
