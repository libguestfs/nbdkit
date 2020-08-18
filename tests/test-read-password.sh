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

# This is an executable C script using nbdkit-cc-plugin.
plugin=$SRCDIR/test-read-password-plugin.c
requires test -x $plugin

requires_unix_domain_sockets

# Since we are matching on error messages.
export LANG=C

f=test-read-password.file
tf=test-read-password.tmp
out=test-read-password.out
rm -f $f $tf $out
cleanup_fn rm -f $f $tf $out

# Password on the command line.
$plugin -U - -fv file=$f password=abc
grep '^abc$' $f

# Password +FILENAME.
echo def > $tf
$plugin -U - -fv file=$f password=+$tf
grep '^def$' $f

# Password +FILENAME, zero length.
: > $tf
$plugin -U - -fv file=$f password=+$tf
test -f $f
! test -s $f

# Password -FD.
echo 123 > $tf
exec 3< $tf
$plugin -U - -fv file=$f password=-3
exec 3<&-
grep '^123$' $f

# Password -FD, zero length.
: > $tf
exec 3< $tf
$plugin -U - -fv file=$f password=-3
exec 3<&-
test -f $f
! test -s $f

# Reading a password from stdin/stdout/stderr using -0/-1/-2 should fail.
for i in 0 1 2; do
    if $plugin -U - -fv file=$f password=-$i </dev/null >&$out ; then
        echo "$0: expected password=-$i to fail"
        exit 1
    fi
    cat $out
    grep "cannot use password -FD" $out
done
