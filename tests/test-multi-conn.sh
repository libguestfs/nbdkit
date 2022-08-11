#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018-2022 Red Hat Inc.
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

# Demonstrate various multi-conn filter behaviors.

source ./functions.sh
set -e
set -x

requires_plugin sh
requires_nbdsh_uri
requires dd iflag=count_bytes </dev/null

files="test-multi-conn.out test-multi-conn.stat"
rm -f $files
cleanup_fn rm -f $files

fail=0
export handles preamble uri
uri= # will be set by --run later
handles=3
preamble='
import os

uri = os.environ["uri"]
handles = int(os.environ["handles"])
h = []
for i in range(handles):
  h.append(nbd.NBD())
  h[i].connect_uri(uri)
print(h[0].can_multi_conn())
'

# Demonstrate the caching present without use of filter
for filter in '' '--filter=multi-conn multi-conn-mode=plugin'; do
  nbdkit -vf -U - sh test-multi-conn-plugin.sh $filter \
    --run 'handles=4 nbdsh -c "$preamble" -c "
# Without flush, reads cache, and writes do not affect persistent data
print(bytes(h[0].pread(4, 0)))
h[1].pwrite(b'\''next '\'', 0)
print(bytes(h[0].pread(4, 0)))
print(bytes(h[1].pread(4, 0)))
print(bytes(h[2].pread(4, 0)))
# Flushing an unrelated connection does not make writes persistent
h[2].flush()
print(bytes(h[0].pread(4, 0)))
print(bytes(h[1].pread(4, 0)))
print(bytes(h[2].pread(4, 0)))
# After write is flushed, only connections without cache see new data
h[1].flush()
print(bytes(h[0].pread(4, 0)))
print(bytes(h[1].pread(4, 0)))
print(bytes(h[2].pread(4, 0)))
print(bytes(h[3].pread(4, 0)))
# Flushing before reads clears the cache
h[0].flush()
h[2].flush()
print(bytes(h[0].pread(4, 0)))
print(bytes(h[2].pread(4, 0)))
"' > test-multi-conn.out || fail=1
  diff -u <(cat <<\EOF
False
b'Init'
b'Init'
b'next'
b'Init'
b'Init'
b'next'
b'Init'
b'Init'
b'next'
b'Init'
b'next'
b'next'
b'next'
EOF
           ) test-multi-conn.out || fail=1
done

# Demonstrate specifics of FUA flag
for filter in '' '--filter=multi-conn multi-conn-mode=plugin'; do
  nbdkit -vf -U - sh test-multi-conn-plugin.sh $filter \
    --run 'nbdsh -c "$preamble" -c "
# Some servers let FUA flush all outstanding requests
h[0].pwrite(b'\''hello '\'', 0)
h[0].pwrite(b'\''world.'\'', 6, nbd.CMD_FLAG_FUA)
print(bytes(h[1].pread(12, 0)))
"' > test-multi-conn.out || fail=1
  diff -u <(cat <<\EOF
False
b'hello world.'
EOF
           ) test-multi-conn.out || fail=1
done
for filter in '' '--filter=multi-conn multi-conn-mode=plugin'; do
  nbdkit -vf -U - sh test-multi-conn-plugin.sh strictfua=1 $filter \
    --run 'nbdsh -c "$preamble" -c "
# But it is also compliant for a server that only flushes the exact request
h[0].pwrite(b'\''hello '\'', 0)
h[0].pwrite(b'\''world.'\'', 6, nbd.CMD_FLAG_FUA)
print(bytes(h[1].pread(12, 0)))
# Without multi-conn, data flushed in one connection can later be reverted
# by a flush of earlier data in another connection
h[1].pwrite(b'\''H'\'', 0, nbd.CMD_FLAG_FUA)
h[2].flush()
print(bytes(h[2].pread(12, 0)))
h[0].flush()
h[2].flush()
print(bytes(h[2].pread(12, 0)))
h[1].flush()
h[2].flush()
print(bytes(h[2].pread(12, 0)))
"' > test-multi-conn.out || fail=1
  diff -u <(cat <<\EOF
False
b'Initiaworld.'
b'Hnitiaworld.'
b'hello world.'
b'Hello world.'
EOF
           ) test-multi-conn.out || fail=1
done

# Demonstrate multi-conn effects.  The cache filter in writeback
# mode is also able to supply multi-conn by a different technique.
for filter in '--filter=multi-conn' 'strictfua=1 --filter=multi-conn' \
              '--filter=multi-conn multi-conn-mode=plugin --filter=cache' ; do
  nbdkit -vf -U - sh test-multi-conn-plugin.sh $filter \
    --run 'nbdsh -c "$preamble" -c "
# FUA writes are immediately visible on all connections
h[0].cache(12, 0)
h[1].pwrite(b'\''Hello '\'', 0, nbd.CMD_FLAG_FUA)
print(bytes(h[0].pread(12, 0)))
# A flush on an unrelated connection makes all other connections consistent
h[1].pwrite(b'\''world.'\'', 6)
h[2].flush()
print(bytes(h[0].pread(12, 0)))
"' > test-multi-conn.out || fail=1
  diff -u <(cat <<\EOF
True
b'Hello l cont'
b'Hello world.'
EOF
           ) test-multi-conn.out || fail=1
done

# unsafe mode intentionally lacks consistency, use at your own risk
nbdkit -vf -U - sh test-multi-conn-plugin.sh \
  --filter=multi-conn multi-conn-mode=unsafe \
  --run 'nbdsh -c "$preamble" -c "
h[0].cache(12, 0)
h[1].pwrite(b'\''Hello '\'', 0, nbd.CMD_FLAG_FUA)
print(bytes(h[0].pread(12, 0)))
h[1].pwrite(b'\''world.'\'', 6)
h[2].flush()
print(bytes(h[0].pread(12, 0)))
"' > test-multi-conn.out || fail=1
diff -u <(cat <<\EOF
True
b'Initial cont'
b'Initial cont'
EOF
         ) test-multi-conn.out || fail=1

# auto mode devolves to multi-conn disable when connections are serialized
nbdkit -vf -U - sh test-multi-conn-plugin.sh --filter=noparallel \
  serialize=connections --filter=multi-conn --filter=cache \
  --run 'handles=1 nbdsh -c "$preamble"
' > test-multi-conn.out || fail=1
diff -u <(cat <<\EOF
False
EOF
         ) test-multi-conn.out || fail=1

# Use --filter=stats to show track-dirty effects
for level in off connection fast; do
  for mode in emulate 'emulate --filter=cache' \
              plugin 'plugin --filter=cache'; do
    echo "setup: $level $mode" >> test-multi-conn.stat
    # Flush with no activity
    nbdkit -vf -U - sh test-multi-conn-plugin.sh --filter=multi-conn \
      --filter=stats statsfile=test-multi-conn.stat statsappend=true \
      multi-conn-track-dirty=$level multi-conn-mode=$mode \
      --run 'nbdsh -c "$preamble" -c "
h[0].flush()
h[0].pread(1, 0)
h[0].flush()
"' > test-multi-conn.out || fail=1
    # Client that flushes assuming multi-conn semantics
    nbdkit -vf -U - sh test-multi-conn-plugin.sh --filter=multi-conn \
      --filter=stats statsfile=test-multi-conn.stat statsappend=true \
      multi-conn-track-dirty=$level multi-conn-mode=$mode \
      --run 'handles=4 nbdsh -c "$preamble" -c "
h[0].pread(1, 0)
h[1].zero(1, 0)
h[3].flush()
h[2].zero(1, 1)
h[0].pread(1, 0)
h[3].flush()
h[3].flush()
"' > test-multi-conn.out || fail=1
    # Client that flushes assuming inconsistent semantics
    nbdkit -vf -U - sh test-multi-conn-plugin.sh --filter=multi-conn \
      --filter=stats statsfile=test-multi-conn.stat statsappend=true \
      multi-conn-track-dirty=$level multi-conn-mode=$mode \
      --run 'nbdsh -c "$preamble" -c "
h[0].pread(1, 0)
h[1].trim(1, 0)
h[0].flush()
h[1].flush()
h[0].pread(1, 0)
h[2].trim(1, 1)
h[0].flush()
h[2].flush()
"' > test-multi-conn.out || fail=1
  done
done
cat test-multi-conn.stat
diff -u <(cat <<\EOF
setup: off emulate
flush: 6 ops
flush: 12 ops
flush: 12 ops
setup: off emulate --filter=cache
flush: 6 ops
flush: 12 ops
flush: 12 ops
setup: off plugin
flush: 2 ops
flush: 3 ops
flush: 4 ops
setup: off plugin --filter=cache
flush: 2 ops
flush: 3 ops
flush: 4 ops
setup: connection emulate
flush: 4 ops
flush: 4 ops
setup: connection emulate --filter=cache
flush: 4 ops
flush: 4 ops
setup: connection plugin
flush: 3 ops
flush: 4 ops
setup: connection plugin --filter=cache
flush: 2 ops
flush: 2 ops
setup: fast emulate
flush: 8 ops
flush: 6 ops
setup: fast emulate --filter=cache
flush: 8 ops
flush: 6 ops
setup: fast plugin
flush: 2 ops
flush: 2 ops
setup: fast plugin --filter=cache
flush: 2 ops
flush: 2 ops
EOF
         ) <($SED -n 's/\(flush:.*ops\).*/\1/p; /^setup:/p' \
                 test-multi-conn.stat) || fail=1

exit $fail
