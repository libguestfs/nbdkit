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

# Test the use of the directory mode of the file plugin.

source ./functions.sh
set -e
set -x

# requires nbdinfo --version # nbdinfo 1.3.9 was broken, so check this instead:
requires nbdkit -U - memory 1 --run 'nbdinfo --size --json "$uri"'
requires jq --version

files="file-dir file-dir.out file-dir.witness"
rm -rf $files
cleanup_fn rm -rf $files
fail=0

# do_nbdkit_fail NAME
# Check that attempting to connect to export NAME fails
do_nbdkit_fail ()
{
    # The --run script occurs only if nbdkit gets past .config_complete;
    # testing for witness proves that our failure was during .open and
    # not at some earlier point
    rm -f file-dir.witness
    nbdkit -U - -v -e "$1" file dir=file-dir \
        --run 'touch file-dir.witness; nbdsh -u "$uri" -c "quit()"' && fail=1
    test -f file-dir.witness || fail=1
}

# do_nbdkit_pass NAME DATA
# Check that export NAME serves DATA as its first byte
do_nbdkit_pass ()
{
    out=$(nbdkit -U - -v -e "$1" file dir=file-dir \
        --run 'nbdsh -u "$uri" -c "print (h.pread (1, 0).decode (\"utf-8\"))"')
    test "$out" = "$2" || fail=1
}

# Not possible to serve a missing directory
nbdkit -vf file dir=nosuchdir && fail=1

# Serving an empty directory
mkdir file-dir
do_nbdkit_fail ''
do_nbdkit_fail 'a'
do_nbdkit_fail '..'
do_nbdkit_fail '/'

# Serving a directory with one file
echo 1 > file-dir/a
do_nbdkit_fail ''
do_nbdkit_pass a 1
do_nbdkit_fail b

# Serving a directory with multiple files.
echo 2 > file-dir/b
do_nbdkit_fail ''
do_nbdkit_pass 'a' 1
do_nbdkit_pass 'b' 2
do_nbdkit_fail 'c'

# Serving a directory with non-regular files
ln -s b file-dir/c
mkfifo file-dir/d
mkdir file-dir/e
ln -s /dev/null file-dir/f
ln -s . file-dir/g
ln -s dangling file-dir/h
do_nbdkit_pass 'a' 1
do_nbdkit_pass 'b' 2
do_nbdkit_pass 'c' 2
do_nbdkit_fail 'd'
do_nbdkit_fail 'e'
do_nbdkit_fail 'f'
do_nbdkit_fail 'g'
do_nbdkit_fail 'h'
do_nbdkit_fail './a'
do_nbdkit_fail '../file-dir/a'

exit $fail
