#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2018 Red Hat Inc.
# All rights reserved.
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

# Test export flags.
#
# Run nbdkit with various can_* callbacks defined and with or without
# the -r flag, and check that nbdkit constructs the export flags
# controlling READ_ONLY, ROTATIONAL, SEND_TRIM, etc. as expected.
#
# We use the oldstyle (-o) protocol here because it's simpler to read
# out the eflags.  We use the shell plugin because it gives maximum
# control over the can_* callbacks (at least, max without having to
# write a C plugin).

source ./functions.sh
set -e

if ! socat -h >/dev/null; then
    echo "$0: 'socat' command not available"
    exit 77
fi

# Check 'od' command exists.
if ! od </dev/null >/dev/null; then
    echo "$0: 'od' command not available"
    exit 77
fi

files="eflags.out"
rm -f $files
cleanup_fn rm -f $files

# The export flags.
# See also src/protocol.h
HAS_FLAGS=$((         1 << 0 ))
READ_ONLY=$((         1 << 1 ))
SEND_FLUSH=$((        1 << 2 ))
SEND_FUA=$((          1 << 3 ))
ROTATIONAL=$((        1 << 4 ))
SEND_TRIM=$((         1 << 5 ))
SEND_WRITE_ZEROES=$(( 1 << 6 ))

all_flags="HAS_FLAGS READ_ONLY SEND_FLUSH SEND_FUA ROTATIONAL SEND_TRIM SEND_WRITE_ZEROES"

do_nbdkit ()
{
    nbdkit -v -U - -o "$@" sh - --run '
        socat -b 28 unix-connect:$unixsocket \
            exec:"dd bs=1 skip=26 count=2 of=eflags.out",cool-write
    '

    # Protocol is big endian, we want native endian.
    eflags=$(( $(od -An -N1     -tu1 eflags.out) << 8 |
               $(od -An -N1 -j1 -tu1 eflags.out)       ))

    # Print the eflags in hex and text.
    printf "eflags 0x%04x" $eflags
    for f in $all_flags; do
        [ $(( eflags & ${!f} )) -ne 0 ] && echo -n " $f"
    done
    echo
}

fail ()
{
    echo "$@"
    exit 1
}

#----------------------------------------------------------------------
# can_write=false

do_nbdkit <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY )) ] ||
    fail "expected HAS_FLAGS|READ_ONLY"

#----------------------------------------------------------------------
# -r
# can_write=false

do_nbdkit -r <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY )) ] ||
    fail "expected HAS_FLAGS|READ_ONLY"

#----------------------------------------------------------------------
# can_write=true
#
# NBD_FLAG_SEND_WRITE_ZEROES is always set on writable connections
# even if can_zero returns false, because nbdkit reckons it can
# emulate zeroing using pwrite.

do_nbdkit <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 0 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|SEND_WRITE_ZEROES )) ] ||
    fail "expected HAS_FLAGS|SEND_WRITE_ZEROES"

#----------------------------------------------------------------------
# -r
# can_write=true
#
# The -r flag overrides the plugin so this behaves as if can_write is
# false.

do_nbdkit -r <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 0 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY )) ] ||
    fail "expected HAS_FLAGS|READ_ONLY"

#----------------------------------------------------------------------
# can_write=false
# can_trim=true
# can_zero=true
#
# If writing is not possible then trim and zero are always disabled.

do_nbdkit <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 3 ;;
     can_trim) exit 0 ;;
     can_zero) exit 0 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY )) ] ||
    fail "expected HAS_FLAGS|READ_ONLY"

#----------------------------------------------------------------------
# -r
# can_write=false
# can_trim=true
# can_zero=true
#
# This is a formality, but check it's the same as above.

do_nbdkit -r <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 3 ;;
     can_trim) exit 0 ;;
     can_zero) exit 0 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY )) ] ||
    fail "expected HAS_FLAGS|READ_ONLY"

#----------------------------------------------------------------------
# can_write=true
# can_trim=true

do_nbdkit <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 0 ;;
     can_trim) exit 0 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|SEND_TRIM|SEND_WRITE_ZEROES )) ] ||
    fail "expected HAS_FLAGS|SEND_TRIM|SEND_WRITE_ZEROES"

#----------------------------------------------------------------------
# can_write=true
# is_rotational=true

do_nbdkit <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 0 ;;
     is_rotational) exit 0 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|ROTATIONAL|SEND_WRITE_ZEROES )) ] ||
    fail "expected HAS_FLAGS|ROTATIONAL|SEND_WRITE_ZEROES"

#----------------------------------------------------------------------
# -r
# can_write=true
# is_rotational=true

do_nbdkit -r <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 0 ;;
     is_rotational) exit 0 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|ROTATIONAL )) ] ||
    fail "expected HAS_FLAGS|READ_ONLY|ROTATIONAL"

#----------------------------------------------------------------------
# can_write=true
# can_fua=native

do_nbdkit <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 0 ;;
     can_fua) echo "native" ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|SEND_FUA|SEND_WRITE_ZEROES )) ] ||
    fail "expected HAS_FLAGS|SEND_FUA|SEND_WRITE_ZEROES"

#----------------------------------------------------------------------
# -r
# can_write=true
# can_fua=native
#
# Setting read-only should ignore can_fua.

do_nbdkit -r <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 0 ;;
     can_fua) echo "native" ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY )) ] ||
    fail "expected HAS_FLAGS|READ_ONLY"
