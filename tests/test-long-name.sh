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

fail=0

# Test handling of NBD maximum string length of 4k.

requires_unix_domain_sockets
requires qemu-io --version
requires qemu-nbd --version
requires nbdsh -c 'exit(not h.supports_uri())'

name16=1234567812345678
name64=$name16$name16$name16$name16
name256=$name64$name64$name64$name64
name1k=$name256$name256$name256$name256
name4k=$name1k$name1k$name1k$name1k
almost4k=${name4k%8$name16}

# Test that $exportname and $uri reflect the name
out=$(nbdkit -U - -e $name4k null --run 'echo $exportname')
if test "$name4k" != "$out"; then
    echo "$0: \$exportname contains wrong contents" >&2
    fail=1
fi
out=$(nbdkit -U - -e $name4k null --run 'echo "$uri"')
case $out in
    nbd+unix:///$name4k\?socket=*) ;;
    *) echo "$0: \$uri contains wrong contents" >&2
       fail=1 ;;
esac
pick_unused_port
out=$(nbdkit -i localhost -p $port -e $name4k null --run 'echo "$uri"')
case $out in
    nbd://localhost:$port/$name4k) ;;
    *) echo "$0: \$uri contains wrong contents" >&2
       fail=1 ;;
esac

# Use largest possible export name, then oversize, with NBD_OPT_EXPORT_NAME.
nbdkit -U - --mask-handshake=0 null --run 'qemu-io -r -f raw -c quit \
  nbd+unix:///'$name4k'\?socket=$unixsocket' || fail=1
# qemu 4.1 did not length check, letting it send an invalid NBD client
# request which nbdkit must filter out. Later qemu might refuse to
# send the request (like libnbd does), at which point this is no longer
# testing nbdkit proper, so we may remove it later:
nbdkit -U - --mask-handshake=0 null --run 'qemu-io -r -f raw -c quit \
  nbd+unix:///'a$name4k'\?socket=$unixsocket' && fail=1

# Repeat with NBD_OPT_GO.
nbdkit -U - null --run 'qemu-io -r -f raw -c quit \
  nbd+unix:///'$name4k'\?socket=$unixsocket' || fail=1
# See above comment about whether this is testing nbdkit or qemu:
nbdkit -U - null --run 'qemu-io -r -f raw -c quit \
  nbd+unix:///'a$name4k'\?socket=$unixsocket' && fail=1

# Use nbdsh to provoke an extremely large NBD_OPT_SET_META_CONTEXT.
nbdkit -U - -e $almost4k null --run 'export exportname uri
nbdsh -c - <<\EOF
import os
long = os.environ["exportname"]
h.set_export_name (long)
h.add_meta_context ("a" + long)
h.add_meta_context ("b" + long)
h.add_meta_context ("c" + long)
h.add_meta_context ("d" + long)
h.add_meta_context ("e" + long)
h.connect_uri (os.environ["uri"])
assert h.get_size() == 0
EOF
'

# See also test-eval-exports.sh for NBD_OPT_LIST with long name

exit $fail
