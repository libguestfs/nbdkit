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

# Test export flags.
#
# Run nbdkit with various can_* callbacks defined and with or without
# the -r flag, and check that nbdkit constructs the export flags
# controlling READ_ONLY, ROTATIONAL, SEND_TRIM, etc. as expected.
#
# We use the shell plugin because it gives maximum control over the
# can_* callbacks (at least, max without having to write a C plugin).

source ./functions.sh
set -e

requires qemu-nbd --version

# This test uses the ‘qemu-nbd --list’ option added in qemu 4.0.
if ! qemu-nbd --help | grep -sq -- --list; then
    echo "$0: skipping because qemu-nbd does not support the --list option"
    exit 77
fi

files="eflags.out eflags.err"
late_args=
rm -f $files
cleanup_fn rm -f $files

# The export flags.
# See also common/protocol/protocol.h
HAS_FLAGS=$((         1 <<  0 ))
READ_ONLY=$((         1 <<  1 ))
SEND_FLUSH=$((        1 <<  2 ))
SEND_FUA=$((          1 <<  3 ))
ROTATIONAL=$((        1 <<  4 ))
SEND_TRIM=$((         1 <<  5 ))
SEND_WRITE_ZEROES=$(( 1 <<  6 ))
SEND_DF=$((           1 <<  7 ))
CAN_MULTI_CONN=$((    1 <<  8 ))
SEND_RESIZE=$((       1 <<  9 ))
SEND_CACHE=$((        1 << 10 ))
SEND_FAST_ZERO=$((    1 << 11 ))

do_nbdkit ()
{
    # Prepend a check for internal caching to the script on stdin.
    { printf %s '
            if test -f $tmpdir/seen_$1; then
                echo "repeat call to $1" >>'"$PWD/eflags.err"'
            else
                touch $tmpdir/seen_$1
            fi
            '; cat; } | nbdkit -v -U - "$@" sh - $late_args \
        --run 'qemu-nbd --list -k $unixsocket' |
        grep -E "flags: 0x" | grep -Eoi '0x[a-f0-9]+' >eflags.out 2>eflags.err
    printf eflags=; cat eflags.out

    # Convert hex flags to decimal and assign it to $eflags.
    eflags=$(printf "%d" $(cat eflags.out))

    # See if nbdkit failed to cache a callback.
    if test -s eflags.err; then
        echo "error: nbdkit did not cache callbacks properly"
        cat eflags.err
        exit 1
    fi
}

fail ()
{
    echo "error: $@ (actual flags were $(printf 0x%x $eflags))"
    exit 1
}

#----------------------------------------------------------------------
# can_write=false
#
# nbdkit supports DF if client requests SR.

do_nbdkit <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|SEND_DF )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY|SEND_DF"

#----------------------------------------------------------------------
# --no-sr
# can_write=false
#
# When SR is disabled, so is the DF flag.

do_nbdkit --no-sr <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY"

#----------------------------------------------------------------------
# -r
# can_write=false

do_nbdkit -r <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|SEND_DF )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY|SEND_DF"

#----------------------------------------------------------------------
# can_write=true
#
# NBD_FLAG_SEND_WRITE_ZEROES and NBD_FLAG_SEND_FAST_ZERO are set on writable
# connections even when can_zero returns false, because nbdkit reckons it
# can emulate zeroing using pwrite.

do_nbdkit <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 0 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|SEND_WRITE_ZEROES|SEND_DF|SEND_FAST_ZERO )) ] ||
    fail "$LINENO: expected HAS_FLAGS|SEND_WRITE_ZEROES|SEND_DF|SEND_FAST_ZERO"

#----------------------------------------------------------------------
# --filter=nozero
# can_write=true
#
# NBD_FLAG_SEND_WRITE_ZEROES is omitted when a filter says so.

do_nbdkit --filter=nozero <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 0 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|SEND_DF )) ] ||
    fail "$LINENO: expected HAS_FLAGS|SEND_DF"

#----------------------------------------------------------------------
# --no=sr
# --filter=nozero
# can_write=true
#
# Absolute minimum in flags.

do_nbdkit --no-sr --filter=nozero <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 0 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS )) ] ||
    fail "$LINENO: expected HAS_FLAGS"

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

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|SEND_DF )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY|SEND_DF"

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

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|SEND_DF )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY|SEND_DF"

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

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|SEND_DF )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY|SEND_DF"

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

[ $eflags -eq $(( HAS_FLAGS|SEND_TRIM|SEND_WRITE_ZEROES|SEND_DF|SEND_FAST_ZERO )) ] ||
    fail "$LINENO: expected HAS_FLAGS|SEND_TRIM|SEND_WRITE_ZEROES|SEND_DF|SEND_FAST_ZERO"

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

[ $eflags -eq $(( HAS_FLAGS|ROTATIONAL|SEND_WRITE_ZEROES|SEND_DF|SEND_FAST_ZERO )) ] ||
    fail "$LINENO: expected HAS_FLAGS|ROTATIONAL|SEND_WRITE_ZEROES|SEND_DF|SEND_FAST_ZERO"

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

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|ROTATIONAL|SEND_DF )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY|ROTATIONAL|SEND_DF"

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

[ $eflags -eq $(( HAS_FLAGS|SEND_FUA|SEND_WRITE_ZEROES|SEND_DF|SEND_FAST_ZERO )) ] ||
    fail "$LINENO: expected HAS_FLAGS|SEND_FUA|SEND_WRITE_ZEROES|SEND_DF|SEND_FAST_ZERO"

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

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|SEND_DF )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY|SEND_DF"

#----------------------------------------------------------------------
# -r
# can_write=true
# can_flush=true
#
# Setting read-only does not ignore can_flush.

do_nbdkit -r <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 0 ;;
     can_flush) exit 0 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|SEND_FLUSH|SEND_DF )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY|SEND_FLUSH|SEND_DF"

#----------------------------------------------------------------------
# can_write=true
# can_flush=true
#
# When can_flush is true, nbdkit reckons it can emulate fua with flush.

do_nbdkit <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 0 ;;
     can_flush) exit 0 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|SEND_FLUSH|SEND_FUA|SEND_WRITE_ZEROES|SEND_DF|SEND_FAST_ZERO )) ] ||
    fail "$LINENO: expected HAS_FLAGS|SEND_FLUSH|SEND_FUA|SEND_WRITE_ZEROES|SEND_DF|SEND_FAST_ZERO"

#----------------------------------------------------------------------
# can_write=true
# can_flush=true
# can_fua=none
#
# Explicit request for no fua emulation.

do_nbdkit <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 0 ;;
     can_flush) exit 0 ;;
     can_fua) echo "none" ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|SEND_FLUSH|SEND_WRITE_ZEROES|SEND_DF|SEND_FAST_ZERO )) ] ||
    fail "$LINENO: expected HAS_FLAGS|SEND_FLUSH|SEND_WRITE_ZEROES|SEND_DF|SEND_FAST_ZERO"

#----------------------------------------------------------------------
# -r
# can_multi_conn=true

do_nbdkit -r <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_multi_conn) exit 0 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|SEND_DF|CAN_MULTI_CONN )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY|SEND_DF|CAN_MULTI_CONN"

#----------------------------------------------------------------------
# -r
# --filter=noparallel serialize=connections
# can_multi_conn=true
#
# A single-threaded server does not allow multiple connections.

late_args="serialize=connections" do_nbdkit -r --filter=noparallel <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_multi_conn) exit 0 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|SEND_DF )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY|SEND_DF"

#----------------------------------------------------------------------
# -r
# thread_model=serialize_connections
# can_multi_conn=true
#
# A single-threaded server does not allow multiple connections.

do_nbdkit -r <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_multi_conn) exit 0 ;;
     thread_model) echo "serialize_connections" ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|SEND_DF )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY|SEND_DF"

#----------------------------------------------------------------------
# -r
# can_cache=emulate

do_nbdkit -r <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_cache) echo "emulate" ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|SEND_DF|SEND_CACHE )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY|SEND_DF|SEND_CACHE"

#----------------------------------------------------------------------
# -r
# --filter=nocache cachemode=none
# can_cache=emulate
#
# Filters override the plugin's choice of caching.

late_args="cachemode=none" do_nbdkit -r --filter=nocache <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_cache) echo "emulate" ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|SEND_DF )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY|SEND_DF"

#----------------------------------------------------------------------
# -r
# can_fast_zero=true
#
# Fast zero support isn't advertised without regular zero support

do_nbdkit -r <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_fast_zero) exit 0 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|SEND_DF )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY|SEND_DF"

#----------------------------------------------------------------------
# --filter=nozero
# can_write=true
# can_fast_zero=true
#
# Fast zero support isn't advertised without regular zero support

do_nbdkit --filter=nozero <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 0 ;;
     can_fast_zero) exit 0 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|SEND_DF )) ] ||
    fail "$LINENO: expected HAS_FLAGS|SEND_DF"

#----------------------------------------------------------------------
# can_write=true
# can_zero=true
#
# Fast zero support is omitted for a plugin that has .zero but did not opt in

do_nbdkit -r <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 0 ;;
     can_zero) exit 0 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|SEND_DF )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY|SEND_DF"

#----------------------------------------------------------------------
# can_write=true
# can_zero=true
# can_fast_zero=false
#
# Fast zero support is omitted if the plugin says so

do_nbdkit -r <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 0 ;;
     can_zero) exit 0 ;;
     can_fast_zero) exit 3 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|SEND_DF )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY|SEND_DF"

#----------------------------------------------------------------------
# can_write=true
# can_zero=false
# can_fast_zero=false
#
# Fast zero support is omitted if the plugin says so

do_nbdkit -r <<'EOF'
case "$1" in
     get_size) echo 1M ;;
     can_write) exit 0 ;;
     can_fast_zero) exit 3 ;;
     *) exit 2 ;;
esac
EOF

[ $eflags -eq $(( HAS_FLAGS|READ_ONLY|SEND_DF )) ] ||
    fail "$LINENO: expected HAS_FLAGS|READ_ONLY|SEND_DF"
