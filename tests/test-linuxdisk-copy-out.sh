#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018-2020 Red Hat Inc.
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

# Test the linuxdisk plugin with captive nbdkit, as described
# in the man page.

source ./functions.sh
set -e
set -x

requires_unix_domain_sockets
requires guestfish --version
requires qemu-img --version

files="linuxdisk-copy-out.img
       linuxdisk-copy-out.test1 linuxdisk-copy-out.test2
       linuxdisk-copy-out.test3 linuxdisk-copy-out.test4"
rm -f $files
cleanup_fn rm -f $files

nbdkit -f -v -U - \
       --filter=partition \
       linuxdisk $srcdir/../plugins partition=1 label=ROOT \
       --run 'qemu-img convert $nbd linuxdisk-copy-out.img'

# Check the disk content.
guestfish --ro -a linuxdisk-copy-out.img -m /dev/sda <<EOF
# Check some known files and directories exist.
  ll /
  ll /linuxdisk
  is-dir /linuxdisk
  is-file /linuxdisk/Makefile.am

# This reads out all the directory entries and all file contents.
  tar-out / - | cat >/dev/null

# Download some files and compare to local copies.
  download /linuxdisk/Makefile linuxdisk-copy-out.test1
  download /linuxdisk/Makefile.am linuxdisk-copy-out.test2
  download /linuxdisk/nbdkit-linuxdisk-plugin.pod linuxdisk-copy-out.test3
  download /linuxdisk/filesystem.c linuxdisk-copy-out.test4
EOF

# Compare downloaded files to local versions.
cmp linuxdisk-copy-out.test1 $srcdir/../plugins/linuxdisk/Makefile
cmp linuxdisk-copy-out.test2 $srcdir/../plugins/linuxdisk/Makefile.am
cmp linuxdisk-copy-out.test3 $srcdir/../plugins/linuxdisk/nbdkit-linuxdisk-plugin.pod
cmp linuxdisk-copy-out.test4 $srcdir/../plugins/linuxdisk/filesystem.c
