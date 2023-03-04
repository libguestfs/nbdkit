#!/usr/bin/env bash
# nbdkit
# Copyright Red Hat
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
requires nbdsh -c 'print(h.set_full_info)' -c 'exit(not h.supports_tls())'
requires dd iflag=count_bytes </dev/null
requires dd iflag=skip_bytes </dev/null

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

export sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
pid="tls-fallback.pid"

files="$sock $pid"
rm -f $files
cleanup_fn rm -f $files

# Run dual-mode server
start_nbdkit -P $pid -U $sock \
             --tls=on --tls-psk=keys.psk -D nbdkit.tls.session=1 \
             --filter=tls-fallback sh - tlsreadme=$'dummy\n' <<\EOF
check () {
 if test "$1" != true; then
   echo 'EINVAL unexpected tls mode' 2>&1; exit 1
 fi
}
case $1 in
  list_exports) check "$3"; echo INTERLEAVED
     echo hello; echo world
     echo world; echo tour ;;
  default_export) check "$3"; echo hello ;;
  open) check "$4"; echo $3 ;;
  export_description) echo "=$2=" ;;
  get_size) echo "$2" | wc -c ;;
  pread) echo "$2" | dd skip=$4 count=$3 iflag=skip_bytes,count_bytes ;;
  can_write | can_trim) exit 0 ;;
  *) exit 2 ;;
esac
EOF

# Plaintext client sees only dummy volume
nbdsh -c '
import os

def f(name, desc):
  assert name == ""
  assert desc == ""

h.set_opt_mode(True)
h.set_full_info(True)
h.connect_unix(os.environ["sock"])
assert h.opt_list(f) == 1
h.opt_info()
assert h.get_canonical_export_name() == ""
try:
  h.get_export_description()
  assert False
except nbd.Error:
  pass
h.set_export_name("hello")
h.opt_go()
assert h.get_size() == 512
assert not h.can_trim()
assert h.pread(5, 0) == b"dummy"
'

# Encrypted client sees desired volumes
nbdsh -c '
import os

def f(name, desc):
  if name == "hello":
    assert desc == "world"
  elif name == "world":
    assert desc == "tour"
  else:
    assert False

h.set_opt_mode(True)
h.set_full_info(True)
h.set_tls(nbd.TLS_REQUIRE)
h.set_tls_psk_file("keys.psk")
h.set_tls_username("qemu")
h.connect_unix(os.environ["sock"])
assert h.opt_list(f) == 2
h.opt_info()
assert h.get_canonical_export_name() == "hello"
assert h.get_export_description() == "=hello="
h.opt_go()
assert h.get_size() == 6
assert h.can_trim()
assert h.pread(5, 0) == b"hello"
'
