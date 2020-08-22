#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2020 Red Hat Inc.
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

# Test the Containerized Data Importer.  We use a fake podman binary
# so this test is completely self-contained.

source ./functions.sh
set -e
set -x

requires_plugin cdi
requires nbdsh --version
requires truncate --version
# Although we use fake podman, we require the real jq.
requires jq --version

sock=`mktemp -u`
files="cdi.pid $sock"
rm -f $files
cleanup_fn rm -f $files
cleanup_fn rm -rf cdi

# Create the fake podman binary.
rm -rf cdi
mkdir cdi

cat > cdi/podman <<'EOF'
#!/bin/sh -
case "$1" in
    pull) ;;  # ignore
    save)
        if [ "$4" != "-o" ]; then
            echo "unexpected command!"
            exit 1
        fi
        d="$5"
        mkdir "$5"
        echo '{"layers":[{"digest":"sha256:1234"}]}' > "$5"/manifest.json
        truncate -s $((1024*1024)) "$5"/1234
        ;;
    *) exit 1 ;;
esac
EOF
chmod +x cdi/podman
export PATH=$PWD/cdi:$PATH

# Start nbdkit.  It should export the 1M raw file "layer".
start_nbdkit -P cdi.pid -U $sock cdi ignored_parameter

nbdsh --connect "nbd+unix://?socket=$sock" \
      -c 'assert h.get_size() == 1024*1024'
