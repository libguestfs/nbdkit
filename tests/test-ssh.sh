#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018-2022 Red Hat Inc.
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

requires test -f disk
requires nbdcopy --version
requires stat --version

# Check that ssh to localhost will work without any passwords or phrases.
#
# Note this assumes that localhost is running an ssh server, but
# should skip if not.
requires ssh -V
if ! ssh -o PreferredAuthentications=none,publickey -o StrictHostKeyChecking=no localhost echo </dev/null
then
    echo "$0: passwordless/phraseless authentication to localhost not possible"
    exit 77
fi

files="ssh.img ssh2.img"
rm -f $files
cleanup_fn rm -f $files

# Copy 'disk' from the "remote" ssh server to local file 'ssh.img'
nbdkit -v -D ssh.log=2 -U - \
       ssh host=localhost $PWD/disk \
       --run 'nbdcopy "$uri" ssh.img'

# The output should be identical.
cmp disk ssh.img

# Copy local file 'ssh.img' to newly created "remote" 'ssh2.img'
size="$(stat -c %s disk)"
nbdkit -v -D ssh.log=2 -U - \
       ssh host=localhost $PWD/ssh2.img \
       create=true create-size=$size \
       --run 'nbdcopy ssh.img "$uri"'

# The output should be identical.
cmp disk ssh2.img
