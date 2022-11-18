#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2020 Red Hat Inc.
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

# Test cc plugin for OCaml plugins.

source ./functions.sh

set -e
set -x

if test "$SRCDIR" = ""; then
    echo "$0: \$SRCDIR is not set"
    exit 1
fi

script=$SRCDIR/test_ocaml_plugin.ml
if test ! -f "$script"; then
    echo "$0: could not locate test_ocaml_plugin.ml"
    exit 1
fi

requires_plugin cc
requires $OCAMLOPT -version
requires_nbdsh_uri
requires_nbdinfo

# For unclear reasons linking the OCaml plugin fails on macOS. XXX
requires_not test "$(uname)" = "Darwin"

out=test-cc-ocaml.out
cleanup_fn rm -f $out
rm -f $out

nbdkit -U - cc $script a=1 b=2 c=3 d=4 \
       CC="$OCAMLOPT" CFLAGS="-output-obj -runtime-variant _pic -I $SRCDIR/../plugins/ocaml unix.cmxa NBDKit.cmx -cclib -lnbdkitocaml" \
       --run 'nbdinfo --size $uri' > $out
test "$(cat $out)" -eq $((512 * 2048))
