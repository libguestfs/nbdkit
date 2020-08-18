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

source ./functions.sh
set -e
set -x

requires_unix_domain_sockets
requires qemu-io --version

out="test-sh-errors.out"
expected="test-sh-errors.expected"
files="$out $expected"
rm -f $files
cleanup_fn rm -f $files

# do_test 'egrep pattern' ['write_zero']
# (Use of an egrep pattern makes it easy to whitelist other localized error
# outputs that may be system-dependent)
do_test ()
{
    : > $out
    if [ "$2" = write_zero ]; then
        cmd='qemu-io -f raw -c "w -z 0 512" $nbd'
    else
        cmd='qemu-io -r -f raw -c "r 0 1" $nbd'
    fi
    nbdkit -v -U - sh - --run "$cmd"' &&
      echo qemu-io unexpectedly passed >> '$out'; :' >> $out
    if grep "qemu-io unexpectedly passed" $out ||
       ! egrep "$1" $out; then
        echo "$0: output did not match expected data"
        echo "expected: $1"
        echo "output:"
        cat $out
        exit 1
    fi
}

# All caps, no trailing whitespace, ENOMEM
do_test 'Cannot allocate memory' <<'EOF'
case "$1" in
    get_size) echo 64K ;;
    pread) printf ENOMEM >&2; exit 1 ;;
    *) exit 2 ;;
esac
EOF

# Lower case, trailing newline, EPERM
do_test 'Operation not permitted' <<'EOF'
case "$1" in
    get_size) echo 64K ;;
    pread) echo eperm >&2; exit 1 ;;
    *) exit 2 ;;
esac
EOF

# Trailing single-line message, ENOSPC
do_test 'No space left on device' <<'EOF'
case "$1" in
    get_size) echo 64K ;;
    pread) echo ENOSPC custom message does not reach client >&2; exit 1 ;;
    *) exit 2 ;;
esac
EOF

# Multi-line message, ESHUTDOWN
do_test "(Cannot|Can't) send after (transport endpoint|socket) shutdown" <<'EOF'
case "$1" in
    get_size) echo 1M ;;
    pread) printf 'ESHUTDOWN\nline one\nline two\n' >&2; exit 1 ;;
    *) exit 2 ;;
esac
EOF

# Any errno that can't be sent over NBD is flattened to EIO
do_test 'Input/output error' <<'EOF'
case "$1" in
    get_size) echo 1M ;;
    pread) echo EPIPE >&2; exit 1 ;;
    *) exit 2 ;;
esac
EOF

# EINVALID is not a real errno name, and should not be confused with EINVAL
do_test 'Input/output error' <<'EOF'
case "$1" in
    get_size) echo 1M ;;
    pread) echo EINVALID >&2; exit 1 ;;
    *) exit 2 ;;
esac
EOF

# ENOTSUP is special to write zero; it triggers a fallback to pwrite
# Also test that anything on stderr is ignored when the script succeeds
do_test 'No space left on device' write_zero <<'EOF'
case "$1" in
    get_size) echo 'EINVAL ignored' >&2; echo 1M ;;
    can_write | can_zero) echo ENOMEM >&2; exit 0 ;;
    pwrite) echo ENOSPC >&2; exit 1 ;;
    zero) echo ENOTSUP >&2; exit 1 ;;
    *) exit 2 ;;
esac
EOF
