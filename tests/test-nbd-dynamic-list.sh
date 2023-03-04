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

source ./functions.sh
set -e
set -x

# This test works with newer libnbd, showing that dynamic mode affects
# export listing.
requires_plugin sh
requires_nbdsh_uri
requires_nbdinfo

# Does the nbd plugin support dynamic lists?
if ! nbdkit --dump-plugin nbd | grep -sq libnbd_dynamic_list=1; then
    echo "$0: nbd plugin built without dynamic export list support"
    exit 77
fi

base=test-nbd-dynamic-list
sock1=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
sock2=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
pid1="$base.pid1"
pid2="$base.pid2"
files="$sock1 $sock2 $pid1 $pid2 $base.list $base.out1 $base.out2 $base.err"
rm -f $files
cleanup_fn rm -f $files

fail=0

# Start a long-running server with .list_exports and .default_export
# set to varying contents
start_nbdkit -P $pid1 -U $sock1 eval get_size='echo "$2"|wc -c' \
    open='echo "$3"' list_exports="cat '$PWD/$base.list'" \
    default_export="cat '$PWD/$base.list'"

# Long-running nbd bridge, which should pass export list through
start_nbdkit -P $pid2 -U $sock2 nbd socket=$sock1 dynamic-export=true

# check_success_one EXPORT
# - nbdinfo of EXPORT on both servers should succeed, with matching output
check_success_one ()
{
    nbdinfo --no-content "nbd+unix:///$1?socket=$sock1" |
        grep -v uri: > $base.out1
    nbdinfo --no-content "nbd+unix:///$1?socket=$sock2" |
        grep -v uri: > $base.out2
    cat $base.out2
    diff -u $base.out1 $base.out2
}

# check_success_list
# - nbdinfo --list on both servers should succeed, with matching output
check_success_list ()
{
    nbdinfo --list --json nbd+unix://\?socket=$sock1 |
        grep -v '"uri"' > $base.out1
    nbdinfo --list --json nbd+unix://\?socket=$sock2 |
        grep -v '"uri"' > $base.out2
    cat $base.out2
    diff -u $base.out1 $base.out2
}

# check_success EXPORT... - both sub-tests, on all EXPORTs
check_success()
{
    for exp; do
        check_success_one "$exp"
    done
    check_success_list
}

# check_fail_one EXPORT
# - nbdinfo of EXPORT on both servers should fail
check_fail_one ()
{
    # Work around nbdinfo 1.6.2 bug that had error message but 0 status
    if nbdinfo --no-content "nbd+unix:///$1?socket=$sock1" > $base.out1 \
       2> $base.err && ! grep "server replied with error" $base.err; then
        fail=1
    fi
    if nbdinfo --no-content "nbd+unix:///$1?socket=$sock2" > $base.out2 \
       2> $base.err && ! grep "server replied with error" $base.err; then
        fail=1
    fi
}

# check_fail_list
# - nbdinfo --list on both servers should fail
check_fail_list ()
{
    if nbdinfo --list --json nbd+unix://\?socket=$sock1 > $base.out1; then
        fail=1
    fi
    if nbdinfo --list --json nbd+unix://\?socket=$sock2 > $base.out2; then
        fail=1
    fi
}

# With no file, list_exports and the default export fail,
# but other exports work
check_fail_one ""
check_success_one name
check_fail_list

# With an empty list, there are 0 exports, and any export works
touch $base.list
check_success "" name

# An explicit advertisement of the default export, any export works
echo > $base.list
check_success "" name

# A non-empty default name
echo name > $base.list
check_success "" name

# Multiple exports, with descriptions
cat > $base.list <<EOF
INTERLEAVED
name1
desc1
name2
desc2
EOF
echo name > $base.list
check_success "" name1

# Longest possible name and description
long=$(printf %04096d 1)
echo NAMES+DESCRIPTIONS > $base.list
echo $long >> $base.list
echo $long >> $base.list
check_success "" $long

# An invalid name prevents list, but we can still connect
echo 2$long >> $base.list
check_success_one ""
check_fail_list

exit $fail
