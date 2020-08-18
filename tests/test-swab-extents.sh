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

# Test the effects of swab on extents.

source ./functions.sh
set -e
set -x

requires_unix_domain_sockets
requires nbdsh --base-allocation -c 'exit(not h.supports_uri())'

files="swab-extents.out swab-extents.exp8 swab-extents.exp16
       swab-extents.exp32 swab-extents.exp64"
rm -f $files
cleanup_fn rm -f $files
fail=0

# We will set up a plugin that provides the following fine-grained extents
# pattern, using [D]ata 0, [H]ole 1, [Z]ero 2, or [B]oth 3
#     DDDDDDDD DDDDDDDZ ZZZZZZZB BBBBBBBH HZZBBBBH HHHHHHHD
# in order to see how swab-bits affects rounding to alignment:
# 8   DDDDDDDD DDDDDDDZ ZZZZZZZB BBBBBBBH HZZBBBBH HHHHHHHD
# 16  DDDDDDDD DDDDDDDD ZZZZZZZZ BBBBBBHH DDZZBBHH HHHHHHDD
# 32  DDDDDDDD DDDDDDDD ZZZZZZZZ BBBBHHHH DDDDHHHH HHHHDDDD
# 64  DDDDDDDD DDDDDDDD ZZZZZZZZ HHHHHHHH DDDDDDDD DDDDDDDD

cat > swab-extents.exp8 <<EOF
[(15, 0), (8, 2), (8, 3), (2, 1), (2, 2), (4, 3), (8, 1), (1, 0)]
EOF
cat > swab-extents.exp16 <<EOF
[(16, 0), (8, 2), (6, 3), (2, 1), (2, 0), (2, 2), (2, 3), (8, 1), (2, 0)]
EOF
cat > swab-extents.exp32 <<EOF
[(16, 0), (8, 2), (4, 3), (4, 1), (4, 0), (8, 1), (4, 0)]
EOF
cat > swab-extents.exp64 <<EOF
[(16, 0), (8, 2), (8, 1), (16, 0)]
EOF

# We also want to test plugins that supply all v. just one extent per call
all='
  echo " 0 7"
  echo " 7 8"
  echo "15 8 zero"
  echo "23 8 zero,hole"
  echo "31 2 hole"
  echo "33 2 zero"
  echo "35 4 zero,hole"
  echo "39 8 hole"
  echo "47 1"
'
one='case $4 in
     0 |  1 |  2 |  3 |  4 |  5 |  6      ) echo " 0 7" ;;
                                        7 |\
     8 |  9 | 10 | 11 | 12 | 13 | 14      ) echo " 7 8" ;;
                                       15 |\
    16 | 17 | 18 | 19 | 20 | 21 | 22      ) echo "15 8 zero" ;;
                                       23 |\
    24 | 25 | 26 | 27 | 28 | 29 | 30      ) echo "23 8 zero,hole" ;;
                                       31 |\
    32                                    ) echo "31 2 hole" ;;
         33 | 34                          ) echo "33 2 zero" ;;
                   35 | 36 | 37 | 38      ) echo "35 4 zero,hole" ;;
                                       39 |\
    40 | 41 | 42 | 43 | 44 | 45 | 46      ) echo "39 8 hole" ;;
                                       47 ) echo "47 1" ;;
esac'

# We also need an nbdsh script to parse all extents, coalescing adjacent
# types for simplicity
export script='
size = h.get_size()
offs = 0
entries = []
def f (metacontext, offset, e, err):
    global entries
    global offs
    assert offs == offset
    for length, flags in zip(*[iter(e)] * 2):
        if entries and flags == entries[-1][1]:
            entries[-1] = (entries[-1][0] + length, flags)
        else:
            entries.append((length, flags))
        offs = offs + length
while offs < size:
    h.block_status (size - offs, offs, f)
print (entries)
'

# Now to test the combinations:
for bits in 8 16 32 64; do
    for exts in "$all" "$one"; do
        nbdkit -U - --filter=swab eval swab-bits=$bits \
               get_size='echo 48' pread='exit 1' extents="$exts" \
               --run 'nbdsh --base-allocation -u "$uri" -c "$script"' \
               > swab-extents.out || fail=1
        diff -u swab-extents.exp$bits swab-extents.out || fail=1
    done
done

exit $fail
