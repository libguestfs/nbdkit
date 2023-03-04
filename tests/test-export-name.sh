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
requires nbdsh --version
requires dd iflag=count_bytes </dev/null

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="$sock exportname.pid"
rm -f $files
cleanup_fn rm -f $files

# Create an nbdkit sh plugin which reflects the export name back to
# the caller in the virtual device data and size.
start_nbdkit -P exportname.pid -U $sock \
             sh - <<'EOF'
case "$1" in
    open)
        # The export name is the handle.  nbdkit-sh-plugin removes
        # exactly one trailing \n for us, so we must add one in case
        # the client's handle also has one.
        printf '%s\n' "$3"
        ;;
    get_size)
        # Returns the length of the export name / handle in _bytes_.
        export LC_ALL=C
        echo ${#2}
        ;;
    pread)
        printf %s "$2" | dd skip=$4 count=$3 iflag=skip_bytes,count_bytes
        ;;
    *) exit 2 ;;
esac
EOF

# Try to read back various export names from the plugin.
for e in "" "test" "/" "//" " " "/ " "?" "テスト" "-n" '\\' $'\n' "%%" \
         "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
do
    export e sock
    nbdsh -c '
import os

e = os.environ["e"]
h.set_export_name(e)
h.connect_unix(os.environ["sock"])

size = h.get_size()
print("size=%r e=%r" % (size, e))
assert size == len(e.encode("utf-8"))

# Zero-sized reads are not defined in the NBD protocol.
if size > 0:
    buf = h.pread(size, 0)
    print("buf=%r e=%r" % (buf, e))
    assert buf == e.encode("utf-8")
'
done
