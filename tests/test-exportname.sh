#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2019-2020 Red Hat Inc.
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

requires_plugin sh
requires_nbdinfo
requires_nbdsh_uri
requires nbdsh -c 'print(h.set_full_info)'
requires jq --version

files="exportname.out exportname.sh"
rm -f $files
cleanup_fn rm -f $files

query='[ [.exports[]] | sort_by(."export-name")[] |
  [."export-name", .description, ."export-size"] ]'
fail=0

cat >exportname.sh <<\EOF
case $1 in
  list_exports)
    echo INTERLEAVED
    echo a; echo x
    echo b; echo y
    echo c; echo z
    ;;
  default_export) echo a ;;
  open) echo "$3" ;;
  export_description | get_size)
    case $2 in
      a) echo 1 ;;
      b) echo 2 ;;
      c) echo 3 ;;
      *) exit 1 ;
    esac ;;
  *) exit 2 ;;
esac
EOF
chmod +x exportname.sh

# Establish a baseline
nbdkit -U - sh exportname.sh \
       --run 'nbdinfo --json --list "$uri"' > exportname.out
cat exportname.out
test "$(jq -c "$query" exportname.out)" = \
     '[["a","x",1],["b","y",2],["c","z",3]]'

# Set the default export
nbdkit -U - --filter=exportname sh exportname.sh default-export= \
       --run 'nbdinfo --no-content --json "$uri"' > exportname.out
cat exportname.out
test "$(jq -c "$query" exportname.out)" = '[["","1",1]]'

nbdkit -U - --filter=exportname sh exportname.sh default-export=b \
       --run 'nbdinfo --no-content --json "$uri"' > exportname.out
cat exportname.out
test "$(jq -c "$query" exportname.out)" = '[["b","2",2]]'

# Test export list policies
nbdkit -U - --filter=exportname sh exportname.sh exportname-list=keep \
       --run 'nbdinfo --json --list "$uri"' > exportname.out
cat exportname.out
test "$(jq -c "$query" exportname.out)" = \
     '[["a","x",1],["b","y",2],["c","z",3]]'

nbdkit -U - --filter=exportname sh exportname.sh exportname-list=error \
       --run 'nbdinfo --json --list "$uri"' > exportname.out && fail=1 || :
test ! -s exportname.out

nbdkit -U - --filter=exportname sh exportname.sh exportname-list=empty \
       --run 'nbdinfo --json --list "$uri"' > exportname.out
cat exportname.out
test "$(jq -c "$query" exportname.out)" = '[]'

nbdkit -U - --filter=exportname sh exportname.sh exportname-list=defaultonly \
       --run 'nbdinfo --json --list "$uri"' > exportname.out
cat exportname.out
got="$(jq -c "$query" exportname.out)"
# libnbd 1.4.0 and 1.4.1 differ on whether --list grabs description
test "$got" = '[["a",null,1]]' || test "$got" = '[["a","1",1]]' || fail=1

nbdkit -U - --filter=exportname sh exportname.sh default-export=b \
       exportname-list=defaultonly exportname=a exportname=b \
       --run 'nbdinfo --json --list "$uri"' > exportname.out
cat exportname.out
got="$(jq -c "$query" exportname.out)"
test "$got" = '[["b",null,2]]' || test "$got" = '[["b","2",2]]' || fail=1

nbdkit -U - --filter=exportname sh exportname.sh \
       exportname-list=explicit exportname=b exportname=a \
       --run 'nbdinfo --json --list "$uri"' > exportname.out
cat exportname.out
got="$(jq -c "$query" exportname.out)"
test "$got" = '[["a",null,1],["b",null,2]]' ||
    test "$got" = '[["a","1",1],["b","2",2]]' || fail=1

nbdkit -U - --filter=exportname sh exportname.sh exportname-list=explicit \
       --run 'nbdinfo --json --list "$uri"' > exportname.out
cat exportname.out
test "$(jq -c "$query" exportname.out)" = '[]'

# Test description modes with lists
nbdkit -U - --filter=exportname sh exportname.sh exportdesc=keep \
       --run 'nbdinfo --json --list "$uri"' > exportname.out
cat exportname.out
test "$(jq -c "$query" exportname.out)" = \
     '[["a","x",1],["b","y",2],["c","z",3]]'

nbdkit -U - --filter=exportname sh exportname.sh exportdesc=none \
       --run 'nbdinfo --json --list "$uri"' > exportname.out
cat exportname.out
test "$(jq -c "$query" exportname.out)" = \
     '[["a",null,1],["b",null,2],["c",null,3]]'

nbdkit -U - --filter=exportname sh exportname.sh exportdesc=fixed:hi \
       --run 'nbdinfo --json --list "$uri"' > exportname.out
cat exportname.out
test "$(jq -c "$query" exportname.out)" = \
     '[["a","hi",1],["b","hi",2],["c","hi",3]]'

nbdkit -U - --filter=exportname sh exportname.sh \
       exportdesc=script:'echo $name$name' \
       --run 'nbdinfo --json --list "$uri"' > exportname.out
cat exportname.out
test "$(jq -c "$query" exportname.out)" = \
     '[["a","aa",1],["b","bb",2],["c","cc",3]]'

# Test description modes with connections
nbdkit -U - -e c --filter=exportname sh exportname.sh exportdesc=fixed:hi \
       --run 'nbdinfo --no-content --json "$uri"' > exportname.out
cat exportname.out
test "$(jq -c "$query" exportname.out)" = '[["c","hi",3]]'

nbdkit -U - -e c --filter=exportname sh exportname.sh \
       exportdesc=script:'echo $name$name' \
       --run 'nbdinfo --no-content --json "$uri"' > exportname.out
cat exportname.out
test "$(jq -c "$query" exportname.out)" = '[["c","cc",3]]'

# Test strict mode. Tolerate nbdinfo 1.6.2 which gave invalid JSON but 0 status
st=0
nbdkit -U - --filter=exportname sh exportname.sh exportname-strict=true \
       --run 'nbdinfo --no-content --json "$uri"' > exportname.out || st=$?
cat exportname.out
if test $? = 0; then
    test -s exportname.out && jq -c "$query" exportname.out && fail=1
fi

st=0
nbdkit -U - --filter=exportname sh exportname.sh exportname-strict=true \
       exportname=a exportname=b exportname=c \
       --run 'nbdinfo --no-content --json "$uri"' > exportname.out || st=$?
cat exportname.out
if test $? = 0; then
    test -s exportname.out && jq -c "$query" exportname.out && fail=1
fi

nbdkit -U - --filter=exportname sh exportname.sh exportname-strict=true \
       exportname=a exportname=b exportname= default-export=a\
       --run 'nbdinfo --no-content --json "$uri"' > exportname.out
cat exportname.out
test "$(jq -c "$query" exportname.out)" = '[["a","1",1]]'

nbdkit -U - -e a --filter=exportname sh exportname.sh exportname-strict=true \
       exportname=a exportname=b exportname=c \
       --run 'nbdinfo --no-content --json "$uri"' > exportname.out
cat exportname.out
test "$(jq -c "$query" exportname.out)" = '[["a","1",1]]'

exit $fail
