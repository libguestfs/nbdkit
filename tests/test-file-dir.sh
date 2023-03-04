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

# Test the use of the directory mode of the file plugin.

source ./functions.sh
set -e
set -x

# The dir parameter does not exist in the Windows version
# of the file plugin.
if is_windows; then
    echo "$0: this test needs to be revised to work on Windows"
    exit 77
fi

requires_nbdinfo
requires_nbdsh_uri
requires nbdsh -c 'print(h.set_full_info)'
requires jq --version

files="file-dir file-dir.out"
rm -rf $files
cleanup_fn rm -rf $files
fail=0

# do_nbdkit_list [--no-sort] EXPOUT
# Check that the advertised list of exports matches EXPOUT
do_nbdkit_list ()
{
    sort=' | sort'
    if [ "$1" = --no-sort ]; then
	sort=
	shift
    fi
    nbdkit -U - -v file directory=file-dir \
        --run 'nbdinfo --list --json "$uri"' >file-dir.out
    cat file-dir.out
    diff -u <(jq -c '[.exports[]."export-name"]'"$sort" file-dir.out) \
        <(printf %s\\n "$1") || fail=1
}

nbdsh_connect_fail_script='
import os
try:
  h.connect_uri(os.environ["uri"])
  exit(1)
except nbd.Error:
  pass
'

# do_nbdkit_fail NAME
# Check that attempting to connect to export NAME fails
do_nbdkit_fail ()
{
    nbdkit -U - -v -e "$1" file dir=file-dir \
        --run 'export uri; nbdsh -c "$nbdsh_connect_fail_script"' || fail=1
}

# do_nbdkit_pass NAME DATA
# Check that export NAME serves DATA as its first byte
do_nbdkit_pass ()
{
    out=$(nbdkit -U - -v -e "$1" file dir=file-dir \
        --run 'nbdsh -u "$uri" -c "print(h.pread(1, 0).decode(\"utf-8\"))"')
    test "$out" = "$2" || fail=1
}

# Not possible to serve a missing directory
nbdkit -vf file dir=nosuchdir && fail=1

# Serving an empty directory
mkdir file-dir
do_nbdkit_list '[]'
do_nbdkit_fail ''
do_nbdkit_fail 'a'
do_nbdkit_fail '..'
do_nbdkit_fail '/'

# Serving a directory with one file
echo 1 > file-dir/a
do_nbdkit_list '["a"]'
do_nbdkit_fail ''
do_nbdkit_pass a 1
do_nbdkit_fail b

# Serving a directory with multiple files.
# Use 'find' to match readdir's raw order (a is not always first!)
echo 2 > file-dir/b
raw=$(find file-dir -type f | xargs echo)
exp=$(echo $raw | $SED 's,file-dir/\(.\),"\1",g; s/ /,/')
do_nbdkit_list --no-sort "[$exp]"
do_nbdkit_list '["a","b"]'
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
do_nbdkit_list '["a","b","c"]'
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
