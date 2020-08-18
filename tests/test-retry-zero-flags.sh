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
requires nbdsh -c 'i = nbd.CMD_FLAG_FAST_ZERO
exit(not h.supports_uri())'

files="retry-zero-flags-count retry-zero-flags-open-count"
rm -f $files
cleanup_fn rm -f $files

touch retry-zero-flags-count retry-zero-flags-open-count
start_t=$SECONDS

# Create a custom plugin which will test retrying.
nbdkit -v -U - \
       sh - \
       --filter=retry retry-delay=1 \
       --run 'nbdsh --uri "$uri" -c "
h.zero (512, 0)
try:
    h.zero (512, 0,
            nbd.CMD_FLAG_FUA | nbd.CMD_FLAG_NO_HOLE | nbd.CMD_FLAG_FAST_ZERO)
except nbd.Error as ex:
    assert ex.errno == \"ENOTSUP\"
h.zero (512, 0, nbd.CMD_FLAG_FUA)
       "' <<'EOF'
#!/usr/bin/env bash
case "$1" in
    open)
        # Count how many times the connection is (re-)opened.
        read i < retry-zero-flags-open-count
        echo $((i+1)) > retry-zero-flags-open-count
        ;;
    can_write | can_zero) exit 0 ;;
    can_fua)
        # Drop FUA support on particular reopens
        read i < retry-zero-flags-open-count
	case $i in
            2 | 3) echo none ;;
            *) echo native ;;
        esac
        ;;
    can_fast_zero)
        # Drop fast zero support on particular reopens
        read i < retry-zero-flags-open-count
        case $i in
            3 | 4) exit 3 ;;
            *) exit 0 ;;
        esac
        ;;
    zero)
        # First zero fails, thereafter it works
        read i < retry-zero-flags-count
        ((i++))
        echo $i > retry-zero-flags-count
        if [ $i -le 1 ]; then
            echo "EIO zero failed" >&2
            exit 1
        fi
        ;;

    get_size) echo 512 ;;
    *) exit 2 ;;
esac
EOF

# In this test we should see the following pattern:
# open count 1: both fua and fast_zero supported
# first zero FAILS
# retry and wait 1 seconds
# only fast_zero supported
# first zero succeeds
# second zero FAILS due to missing fua support
# retry and wait 1 seconds
# open count 2: neither fua nor fast_zero supported
# second zero FAILS fast due to missing fast_zero support
# third zero FAILS due to missing fua support
# retry and wait 1 seconds
# open count 3: only fua supported
# third zero succeeds

# The minimum time for the test should be 1+1+1 = 3 seconds.
end_t=$SECONDS
if [ $((end_t - start_t)) -lt 3 ]; then
    echo "$0: test ran too quickly"
    exit 1
fi

# Check the handle was opened 4 times (first open + one reopen for
# each retry).
read open_count < retry-zero-flags-open-count
if [ $open_count -ne 4 ]; then
    echo "$0: open-count ($open_count) != 4"
    exit 1
fi
