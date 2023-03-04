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

# Test the floppy / FAT32 plugin.

source ./functions.sh
set -e

requires_plugin floppy
requires guestfish --version

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="floppy.pid $sock floppy.test1 floppy.test2"
rm -f $files
cleanup_fn rm -f $files

# When testing this we need to use a directory which won't change
# during the test (so not the current directory).
start_nbdkit -P floppy.pid -U $sock floppy $srcdir/../plugins

# Check the floppy content.
guestfish --ro --format=raw -a "nbd://?socket=$sock" -m /dev/sda1 <<'EOF'
  ll /
  ll /floppy/
  ll /iso/

# This reads out all the directory entries and all file contents.
  tar-out / - | cat >/dev/null

# Check some files exist.  This also tests LFN support.
  is-file /floppy/Makefile.am
  is-file /floppy/nbdkit-floppy-plugin.pod

# Download some files and compare to local copies.
  download /floppy/Makefile.am floppy.test1
  download /iso/Makefile.am floppy.test2
EOF

# Compare downloaded files to local versions.
cmp floppy.test1 $srcdir/../plugins/floppy/Makefile.am
cmp floppy.test2 $srcdir/../plugins/iso/Makefile.am
