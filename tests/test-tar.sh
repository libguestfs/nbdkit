#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2017-2020 Red Hat Inc.
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

# Python scripts break valgrind.
if [ "$NBDKIT_VALGRIND" = "1" ]; then
    echo "$0: skipping Python test under valgrind."
    exit 77
fi

requires test -f disk
requires guestfish --version

# The tar plugin is written in Python and uses the tarfile module.
requires python --version
requires python -c 'import tarfile'

sock=`mktemp -u`
files="tar.pid tar.tar $sock"
rm -f $files
cleanup_fn rm -f $files

# Create a tar file containing the disk image.
tar cf tar.tar disk

# Run nbdkit.
start_nbdkit -P tar.pid -U $sock tar tar=tar.tar file=disk

# Now see if we can open, read and write the disk from the tar file.
guestfish -x --format=raw -a "nbd://?socket=$sock" -m /dev/sda1 <<EOF
  # Check for existing file.
  cat /hello.txt

  # Write a new file.
  write /test.txt "hello"
  cat /test.txt
EOF

# Check that the tar file isn't corrupt.
tar tvvf tar.tar
