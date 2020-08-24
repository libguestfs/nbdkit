#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2019-2020 Red Hat Inc.
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
requires nbdsh -c 'exit (not h.supports_tls ())'

# Does the nbdkit binary support TLS?
if ! nbdkit --dump-config | grep -sq tls=yes; then
    echo "$0: nbdkit built without TLS support"
    exit 77
fi

# Did we create the PSK keys file?
# Probably 'certtool' is missing.
if [ ! -s keys.psk ]; then
    echo "$0: PSK keys file was not created by the test harness"
    exit 77
fi

export sock=`mktemp -u`
pid="tls-fallback.pid"

files="$sock $pid"
rm -f $files
cleanup_fn rm -f $files

# Run dual-mode server
start_nbdkit -P $pid -U $sock --tls=on --tls-psk=keys.psk \
    --filter=tls-fallback sh - tlsreadme=$'dummy\n' <<\EOF
case $1 in
  list_exports) echo NAMES; echo hello; echo world ;;
  open) if test "$4" != true; then
      echo 'EINVAL unexpected tls mode' 2>&1; exit 1
    fi
    echo $3 ;;
  get_size) echo "$2" | wc -c ;;
  pread) echo "$2" | dd skip=$4 count=$3 iflag=skip_bytes,count_bytes ;;
  can_write | can_trim) exit 0 ;;
  *) exit 2 ;;
esac
EOF

# Plaintext client sees only dummy volume
nbdsh -c '
import os
h.set_export_name ("hello")
h.connect_unix (os.environ["sock"])
assert h.get_size () == 512
assert not h.can_trim()
assert h.pread (5, 0) == b"dummy"
'

# Encrypted client sees desired volumes
nbdsh -c '
import os
h.set_export_name ("hello")
h.set_tls (nbd.TLS_REQUIRE)
h.set_tls_psk_file ("keys.psk")
h.set_tls_username ("qemu")
h.connect_unix (os.environ["sock"])
assert h.get_size () == 6
assert h.can_trim ()
assert h.pread (5, 0) == b"hello"
'
