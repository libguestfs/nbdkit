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

# Test each option appears in the synopsis.

source ./functions.sh
set -e
set -x

# If these fail it's probably because you ran the script by hand.
test -n "$srcdir"
synopsis=$srcdir/../docs/synopsis.txt
test -f "$synopsis"

for i in $(nbdkit --short-options); do
    grep '[^-]'$i $synopsis
done

for i in $(nbdkit --long-options); do
    case "$i" in
        # Only one version of each long option is shown in the
        # synopsis, so ignore other versions.
        --export-name) ;;       # alias of -e, --exportname
        --no-fork) ;;           # alias of -f
        --ip-addr) ;;           # alias of -i, --ipaddr
        --new-style) ;;         # alias of -n, --newstyle
        --old-style) ;;         # alias of -o, --oldstyle
        --pid-file) ;;          # alias of -P, --pidfile
        --read-only) ;;         # alias of -r, --readonly
        --stdin) ;;             # alias of -s, --single

        # Anything else is tested.
        *)
            grep -- "$i" $synopsis
    esac
done
