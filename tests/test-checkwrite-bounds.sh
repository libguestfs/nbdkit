#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2020-2022 Red Hat Inc.
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

# Regression test for an earlier bug seen in checkwrite filter.
# https://listman.redhat.com/archives/libguestfs/2022-July/029496.html

source ./functions.sh
set -e
set -x

requires_run
requires_filter checkwrite
requires nbdsh --version
requires_nbdsh_uri
requires dd iflag=count_bytes </dev/null

nbdkit -U - sh - --filter=checkwrite <<'EOF' \
       --run 'nbdsh -u "$uri" -c "h.zero (655360, 262144, 0)"'
case "$1" in
  get_size) echo 1048576 ;;
  pread) dd if=/dev/zero count=$3 iflag=count_bytes ;;
  can_extents) exit 0 ;;
  extents)
    echo 0 262144 3
    echo 262144 131072 0
    echo 393216 655360 3
  ;;
  *) exit 2 ;;
esac
EOF
