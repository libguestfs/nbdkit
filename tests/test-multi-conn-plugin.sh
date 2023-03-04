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

# Test plugin used by test-multi-conn.sh.
# This plugin purposefully maintains a per-connection cache.
# An optional parameter tightfua=true controls whether FUA acts on
# just the given region, or on all pending ops in the current connection.
# Note that an earlier cached write on one connection can overwrite a later
# FUA write on another connection - this is okay (the client is buggy if
# it ever sends overlapping writes without coordinating flushes and still
# expects any particular write to occur last).

get_export() {
    case $1 in
        */*) export="$tmpdir/$(dirname $1)" conn=$(basename $1) ;;
        *) export="$tmpdir" conn=$1 ;;
    esac
}
fill_cache() {
    if test ! -f "$export/$conn"; then
        cp "$export/0" "$export/$conn"
    fi
}
do_fua() {
    case ,$3, in
        *,fua,*)
            if test -f "$tmpdir/strictfua"; then
                dd of="$export/0" if="$export/$conn" skip=$2 seek=$2 count=$1 \
                  conv=notrunc iflag=count_bytes,skip_bytes oflag=seek_bytes
            else
                do_flush
            fi ;;
    esac
}
do_flush() {
    if test -f "$export/$conn-replay"; then
        while read cnt off; do
            dd of="$export/0" if="$export/$conn" skip=$off seek=$off count=$cnt \
               conv=notrunc iflag=count_bytes,skip_bytes oflag=seek_bytes
        done < "$export/$conn-replay"
    fi
    rm -f "$export/$conn" "$export/$conn-replay"
}
case "$1" in
    config)
        case $2 in
            strictfua)
                case $3 in
                    true | on | 1) touch "$tmpdir/strictfua" ;;
                    false | off | 0) ;;
                    *) echo "unknown value for strictfua $3" >&2; exit 1 ;;
                esac ;;
            *) echo "unknown config key $2" >&2; exit 1 ;;
        esac
        ;;
    get_ready)
        printf "%-32s" 'Initial contents' > "$tmpdir/0"
        echo 0 > "$tmpdir/counter"
        ;;
    get_size)
        echo 32
        ;;
    can_write | can_zero | can_trim | can_flush)
        exit 0
        ;;
    can_fua | can_cache)
        echo native
        ;;
    open)
        read i < "$tmpdir/counter"
        i=$((i+1))
        echo $i > "$tmpdir/counter"
        if test -z "$3"; then
            echo $i
        else
            mkdir -p "$tmpdir/$3" || exit 1
            cp "$tmpdir/0" "$tmpdir/$3/0"
            echo "$3/$i"
        fi
        ;;
    pread)
        get_export $2
        fill_cache
        dd if="$export/$conn" skip=$4 count=$3 iflag=count_bytes,skip_bytes
        ;;
    cache)
        get_export $2
        fill_cache
        ;;
    pwrite)
        get_export $2
        fill_cache
        dd of="$export/$conn" seek=$4 conv=notrunc oflag=seek_bytes
        echo $3 $4 >> "$export/$conn-replay"
        do_fua $3 $4 $5
        ;;
    zero | trim)
        get_export $2
        fill_cache
        dd of="$export/$conn" if="/dev/zero" count=$3 seek=$4 conv=notrunc\
           oflag=seek_bytes iflag=count_bytes
        echo $3 $4 >> "$export/$conn-replay"
        do_fua $3 $4 $5
        ;;
    flush)
        get_export $2
        do_flush
        ;;
    *)
        exit 2
        ;;
esac
