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

requires nbdsh -c 'print (h.get_list_export_description)'
requires nbdinfo --help
requires jq --version

files="eval-exports.list eval-exports.out"
rm -f $files
cleanup_fn rm -f $files

# do_nbdkit [skip_list] EXPOUT
do_nbdkit ()
{
    # Hack: since we never pass args that would go through .config, we can
    # define a dummy .config to avoid defining .list_export
    hack=
    if test $1 = skip_list; then
        hack=config=
        shift
    else
        cat eval-exports.list
    fi
    nbdkit -U - -v eval ${hack}list_exports='cat eval-exports.list' \
      get_size='echo 0' --run 'nbdinfo --list --json "$uri"' >eval-exports.out
    cat eval-exports.out
    diff -u <(jq -c '[.exports[] | [."export-name", .description]]' \
              eval-exports.out) <(printf %s\\n "$1")
}

# Control case: no .list_exports, which defaults to advertising ""
rm -f eval-exports.list
do_nbdkit skip_list '[["",null]]'

# Various spellings of empty lists, producing 0 exports
for fmt in '' 'NAMES\n' 'INTERLEAVED\n' 'NAMES+DESCRIPTIONS\n'; do
    printf "$fmt" >eval-exports.list
    do_nbdkit '[]'
done

# Various spellings of explicit list for the default export, no description
for fmt in '\n' 'NAMES\n\n' 'INTERLEAVED\n\n' 'INTERLEAVED\n\n\n' \
           'NAMES+DESCRIPTIONS\n\n' 'NAMES+DESCRIPTIONS\n\n\n'; do
    printf "$fmt" >eval-exports.list
    do_nbdkit '[["",null]]'
done

# A non-default name
for fmt in 'name\n' 'NAMES\nname\n'; do
    printf "$fmt" >eval-exports.list
    do_nbdkit '[["name",null]]'
done

# One export with a description
for fmt in 'INTERLEAVED\nname\ndesc\n' 'NAMES+DESCRIPTIONS\nname\ndesc\n'; do
    printf "$fmt" >eval-exports.list
    do_nbdkit '[["name","desc"]]'
done

# Multiple exports, with correct number of lines
for fmt in 'INTERLEAVED\nname 1\ndesc 1\nname 2\ndesc 2\n' \
           'NAMES+DESCRIPTIONS\nname 1\nname 2\ndesc 1\ndesc 2\n'; do
    printf "$fmt" >eval-exports.list
    do_nbdkit '[["name 1","desc 1"],["name 2","desc 2"]]'
done

# Multiple exports, with final description line missing
for fmt in 'INTERLEAVED\nname 1\ndesc 1\nname 2\n' \
           'NAMES+DESCRIPTIONS\nname 1\nname 2\ndesc 1\n'; do
    printf "$fmt" >eval-exports.list
    do_nbdkit '[["name 1","desc 1"],["name 2",null]]'
done

# Largest possible name and description
long=$(printf %04096d 1)
echo NAMES+DESCRIPTIONS >eval-exports.list
echo $long >>eval-exports.list
echo $long >>eval-exports.list
do_nbdkit "[[\"$long\",\"$long\"]]"

# Invalid name (too long) causes an error response to NBD_OPT_LIST
if nbdkit -U - -v eval list_exports="echo 2$long" \
       get_size='echo 0' --run 'nbdinfo --list --json "$uri"'; then
    echo "expected failure"; exit 1
fi
