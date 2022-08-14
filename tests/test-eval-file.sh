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

# This is an example from the nbdkit-eval-plugin(1) manual page.
# Check here that it doesn't regress.

source ./functions.sh
set -e
set -x

requires guestfish --version
requires test -f disk
requires dd iflag=count_bytes </dev/null
requires stat --version

files="eval-file.img"
rm -f $files
cleanup_fn rm -f $files

cp disk eval-file.img

nbdkit -fv -U - eval \
       config='ln -sf "$(realpath "$3")" $tmpdir/file' \
       get_size='stat -Lc %s $tmpdir/file' \
       pread='dd if=$tmpdir/file skip=$4 count=$3 iflag=count_bytes,skip_bytes' \
       pwrite='dd of=$tmpdir/file seek=$4 conv=notrunc oflag=seek_bytes' \
       file=eval-file.img \
       --run '
    guestfish \
        add "" protocol:nbd server:unix:$unixsocket : \
        run : \
        mount /dev/sda1 / : \
        write /hello "hello,world" : \
        cat /hello : \
        fstrim /
'
