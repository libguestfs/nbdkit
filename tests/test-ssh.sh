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

source ./functions.sh
set -e
set -x

requires sshd -t -f ssh/sshd_config
requires qemu-img --version
requires cut --version

# Check that ssh to localhost will work without any passwords or phrases.
requires ssh -V
if ! ssh -o PreferredAuthentications=none,publickey -o StrictHostKeyChecking=no localhost echo </dev/null
then
    echo "$0: passwordless/phraseless authentication to localhost not possible"
    exit 77
fi

files="ssh.img"
rm -f $files
cleanup_fn rm -f $files

`which sshd` -f ssh/sshd_config -D -e &
sshd_pid=$!
cleanup_fn kill $sshd_pid

# Get the sshd port number which was randomly assigned.
port="$(grep ^Port ssh/sshd_config | cut -f 2)"

# Run nbdkit with the ssh plugin to copy a file.
nbdkit -v -D ssh.log=2 -U - \
       ssh host=localhost $PWD/disk \
       --run 'qemu-img convert $nbd ssh.img'

# The output should be identical.
cmp disk ssh.img
