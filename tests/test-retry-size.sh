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

fail=0

requires_unix_domain_sockets
requires qemu-io --version
requires dd iflag=count_bytes </dev/null

files="retry-size-open-count retry-size-fail"
rm -f $files
cleanup_fn rm -f $files

touch retry-size-open-count
start_t=$SECONDS

# Create a custom plugin which will test retrying.
st=0
nbdkit -v -U - \
       sh - \
       --filter=retry retry-delay=1 \
       --run 'qemu-io -f raw -r $nbd \
    -c "r 0 512" -c "r 512 512"' <<'EOF' || st=$?
#!/usr/bin/env bash
case "$1" in
    open)
        # Count how many times the connection is (re-)opened.
        read i < retry-size-open-count
        echo $((i+1)) > retry-size-open-count
        ;;
    get_size)
        # Temporarily report a smaller size
        read i < retry-size-open-count
        if [ $i = 2 ]; then
            echo 512
        else
            echo 1024
        fi
        ;;
    pread)
        # Fail first open unconditionally
        # On second open, ensure nbdkit obyes smaller bound
        # On third open, allow read to succeed
        read i < retry-size-open-count
        case $i in
            1) echo "EIO too soon to read" >&2
               exit 1 ;;
            2) if [ $(( $3 + $4 )) -gt 512 ]; then
                   touch retry-size-fail
               fi ;;
        esac
        dd if=/dev/zero count=$3 iflag=count_bytes
        ;;
    *) exit 2 ;;
esac
EOF

# In this test we should see the following:
# open reports size 1024
# first pread FAILS
# retry and wait 1 seconds
# open reports size 512
# first pread succeeds
# second pread FAILS without calling into pread
# retry and wait 1 seconds
# open reports size 1024
# second pread succeeds

# The minimum time for the test should be 1+1 = 2 seconds.
end_t=$SECONDS
if [ $((end_t - start_t)) -lt 2 ]; then
    echo "$0: test ran too quickly"
    fail=1
fi

# Check the handle was opened 3 times (first open + reopens).
read open_count < retry-size-open-count
if [ $open_count -ne 3 ]; then
    echo "$0: open-count ($open_count) != 3"
    fail=1
fi

# Check that nbdkit checked bounds
if [ -e retry-size-fail ]; then
    echo "$0: nbdkit read past EOF"
    fail=1
fi

exit $fail
