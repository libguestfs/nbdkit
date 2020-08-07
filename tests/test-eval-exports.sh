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

# requires nbdinfo --version # nbdinfo 1.3.9 was broken, so check this instead:
requires nbdkit -U - memory 1 --run 'nbdinfo --size --json "$uri"'
requires jq --version

sock=$(mktemp -u)
files="eval-exports.list eval-exports.out eval-exports.pid $sock"
rm -f $files
cleanup_fn rm -f $files
fail=0

# Control case: no .list_exports, which defaults to advertising ""
rm -f eval-exports.list
nbdkit -U - -v eval \
    open='[ "$3" = "" ] || { echo EINVAL wrong export >&2; exit 1; }' \
    get_size='echo 0' --run 'nbdinfo --list --json "$uri"' > eval-exports.out
cat eval-exports.out
diff -u <(jq -c '[.exports[] | [."export-name", .description]]' \
          eval-exports.out) <(printf %s\\n '[["",null]]')

# Start a long-running server with .list_exports set to varying contents
start_nbdkit -P eval-exports.pid -U $sock eval get_size='echo 0' \
    list_exports="cat '$PWD'/eval-exports.list"

# do_nbdkit EXPNAME EXPOUT
do_nbdkit ()
{
    # Check how the default export name is handled
    # nbdinfo currently makes multiple connections, so we can't use the
    # long-running server for validating default export name.
    nbdkit -U - -v eval list_exports='cat eval-exports.list' \
      open='[ "$3" = "'"$1"'" ] || { echo EINVAL wrong export >&2; exit 1; }' \
      get_size='echo 0' --run 'nbdsh -u "$uri" -c "exit()"'
    # Check what exports are listed
    nbdinfo --list --json nbd+unix://\?socket=$sock >eval-exports.out
    cat eval-exports.out
    diff -u <(jq -c '[.exports[] | [."export-name", .description]]' \
              eval-exports.out) <(printf %s\\n "$2")
}

# With no file, .list_exports fails, but connecting works
nbdinfo --list --json nbd+unix://\?socket=$sock && fail=1
nbdsh -u nbd+unix://\?socket=$sock -c 'quit()'

# Various spellings of empty lists, producing 0 exports
for fmt in '' 'NAMES\n' 'INTERLEAVED\n' 'NAMES+DESCRIPTIONS\n'; do
    printf "$fmt" >eval-exports.list
    do_nbdkit '' '[]'
done

# Various spellings of explicit list for the default export, no description
for fmt in '\n' 'NAMES\n\n' 'INTERLEAVED\n\n' 'INTERLEAVED\n\n\n' \
           'NAMES+DESCRIPTIONS\n\n' 'NAMES+DESCRIPTIONS\n\n\n'; do
    printf "$fmt" >eval-exports.list
    do_nbdkit '' '[["",null]]'
done

# A non-default name
for fmt in 'name\n' 'NAMES\nname\n'; do
    printf "$fmt" >eval-exports.list
    do_nbdkit name '[["name",null]]'
done

# One export with a description
for fmt in 'INTERLEAVED\nname\ndesc\n' 'NAMES+DESCRIPTIONS\nname\ndesc\n'; do
    printf "$fmt" >eval-exports.list
    do_nbdkit name '[["name","desc"]]'
done

# Multiple exports, with correct number of lines
for fmt in 'INTERLEAVED\nname 1\ndesc 1\nname 2\ndesc 2\n' \
           'NAMES+DESCRIPTIONS\nname 1\nname 2\ndesc 1\ndesc 2\n'; do
    printf "$fmt" >eval-exports.list
    do_nbdkit 'name 1' '[["name 1","desc 1"],["name 2","desc 2"]]'
done

# Multiple exports, with final description line missing
for fmt in 'INTERLEAVED\nname 1\ndesc 1\nname 2\n' \
           'NAMES+DESCRIPTIONS\nname 1\nname 2\ndesc 1\n'; do
    printf "$fmt" >eval-exports.list
    do_nbdkit 'name 1' '[["name 1","desc 1"],["name 2",null]]'
done

# Largest possible name and description
long=$(printf %04096d 1)
echo NAMES+DESCRIPTIONS >eval-exports.list
echo $long >>eval-exports.list
echo $long >>eval-exports.list
do_nbdkit $long "[[\"$long\",\"$long\"]]"

# Invalid name (too long) causes an error response to NBD_OPT_LIST
nbdkit -U - -v eval list_exports="echo 2$long" \
       get_size='echo 0' --run 'nbdinfo --list --json "$uri"' && fail=1

exit $fail
