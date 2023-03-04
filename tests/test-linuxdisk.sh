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

# Test the linuxdisk plugin.

source ./functions.sh
set -e
set -x

requires_plugin linuxdisk
requires guestfish --version
requires mkfifo --version

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
d=linuxdisk.d
rm -rf $d
cleanup_fn rm -rf $d
cleanup_fn rm -f $sock

# Create a test directory with some regular files, subdirectories and
# special files.
mkdir $d
mkfifo $d/fifo
mkdir $d/sub
cp $srcdir/Makefile.am $d/sub/Makefile.am
ln $d/sub/Makefile.am $d/sub/hardlink
ln -s $d/sub/Makefile.am $d/sub/symlink

# It would be nice to use the Unix domain socket to test that the
# socket gets created, but in fact that won't work because this socket
# isn't created until after the plugin creates the virtual disk.
start_nbdkit -P $d/linuxdisk.pid \
             -U $sock \
             linuxdisk $d

# Check the disk content.
guestfish --ro --format=raw -a "nbd://?socket=$sock" -m /dev/sda1 <<EOF
  ll /
  ll /sub

# Check regular files exist.
  is-file /sub/Makefile.am
  is-file /sub/hardlink
# XXX Test sparse files in future.

# Check the specials exist.
  is-fifo /fifo
  is-symlink /sub/symlink
# XXX Test sockets, etc. in future.

# Check hard linked files.
  lstatns /sub/Makefile.am | cat > $d/nlink.1
  lstatns /sub/hardlink | cat > $d/nlink.2

# This reads out all the directory entries and all file contents.
  tar-out / - | cat >/dev/null

# Download file and compare to local copy.
  download /sub/Makefile.am $d/Makefile.am
EOF

# Check the two hard linked files have st_nlink == 2.
grep "st_nlink: 2" $d/nlink.1
grep "st_nlink: 2" $d/nlink.2

# Compare downloaded file to local version.
cmp $d/Makefile.am $srcdir/Makefile.am
