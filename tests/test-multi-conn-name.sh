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

# Demonstrate effect of multi-conn-exportname config flag

source ./functions.sh
set -e
set -x

requires_plugin sh
requires_nbdsh_uri
requires nbdsh -c 'print(h.set_opt_mode)'
requires dd iflag=count_bytes </dev/null

files="test-multi-conn-name.out"
rm -f $files
cleanup_fn rm -f $files

fail=0
export script='
import os

uri = os.environ["uri"]
h = {}
for name in ["a", "b"]:
  for conn in [1, 2]:
    key = "%s%d" % (name, conn)
    h[key] = nbd.NBD()
    h[key].set_opt_mode(True)
    h[key].connect_uri(uri)
    h[key].set_export_name(name)
    h[key].opt_go()
h["a1"].pread(1, 0)
h["a2"].pwrite(b"A", 0)
h["b1"].pread(1, 0)
h["b2"].pwrite(b"B", 0, nbd.CMD_FLAG_FUA)
print(bytes(h["a1"].pread(1, 0)))
print(bytes(h["b1"].pread(1, 0)))
'

# Without the knob we flush all exports
nbdkit -vf -U - sh test-multi-conn-plugin.sh --filter=multi-conn \
  --run 'export uri; nbdsh -c "$script"' > test-multi-conn-name.out || fail=1
diff -u <(cat <<\EOF
b'A'
b'B'
EOF
         ) test-multi-conn-name.out || fail=1
# But with the knob, our flush is specific to the correct export
nbdkit -vf -U - sh test-multi-conn-plugin.sh --filter=multi-conn \
  multi-conn-exportname=true \
  --run 'export uri; nbdsh -c "$script"' > test-multi-conn-name.out || fail=1
diff -u <(cat <<\EOF
b'I'
b'B'
EOF
         ) test-multi-conn-name.out || fail=1

exit $fail
