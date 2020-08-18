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

files="retry-readonly-count retry-readonly-open-count"
rm -f $files
cleanup_fn rm -f $files

touch retry-readonly-count retry-readonly-open-count
start_t=$SECONDS

# Create a custom plugin which will test retrying.
nbdkit -v -U - \
       sh - \
       --filter=retry retry-delay=1 retry-readonly=yes \
       --run 'qemu-io -f raw -c "w 0 512" -c "w 0 512" $nbd || :' <<'EOF'
#!/usr/bin/env bash
case "$1" in
    open)
        # Count how many times the connection is (re-)opened.
        read i < retry-readonly-open-count
        echo $((i+1)) > retry-readonly-open-count
        ;;
    can_write) ;;
    pwrite)
        # Fail 3 times then succeed - however, force-readonly means we will
        # never return here after the first failure.
        read i < retry-readonly-count
        ((i++))
        echo $i > retry-readonly-count
        if [ $i -le 3 ]; then
            echo "EIO pwrite failed" >&2
            exit 1
        else
            : drop data
        fi
        ;;

    get_size) echo 512 ;;
    *) exit 2 ;;
esac
EOF

# In this test we should see 3 failures:
# first pwrite FAILS
# retry and wait 1 seconds
# first pwrite FAILS immediately now that connection is readonly
# second pwrite FAILS immediately

# The minimum time for the test should be 1 second.
end_t=$SECONDS
if [ $((end_t - start_t)) -lt 1 ]; then
    echo "$0: test ran too quickly"
    exit 1
fi

# Check the handle was opened 2 times (first open + one reopen).
read open_count < retry-readonly-open-count
if [ $open_count -ne 2 ]; then
    echo "$0: open-count ($open_count) != 2"
    exit 1
fi
