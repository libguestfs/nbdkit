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

# This test is similar to test-retry.sh but it also tests the case
# where the reopen operation fails.

source ./functions.sh
set -e
set -x

fail=0

requires_unix_domain_sockets
requires qemu-io --version
requires dd iflag=count_bytes </dev/null

files="retry-reopen-fail-count retry-reopen-fail-open-count
       retry-reopen-fail-status"
rm -f $files
cleanup_fn rm -f $files

# do_test retries mintime expcount status
do_test ()
{
    retries=$1
    mintime=$2
    expcount=$3
    status=$4

    echo 0 > retry-reopen-fail-count
    echo 0 > retry-reopen-fail-open-count
    : > retry-reopen-fail-status
    start_t=$SECONDS

    # Create a custom plugin which will test retrying.
    nbdkit -v -U - \
           sh - \
           --filter=retry retry-delay=1 retries=$retries \
           --run 'qemu-io -r -f raw $nbd -c "r 0 512" -c "r 0 512"
                  echo $? >> retry-reopen-fail-status' <<'EOF'
#!/usr/bin/env bash
handle=$2
check_handle () {
    if [ x"$handle" != xhandle ]; then
        echo 22 >> retry-reopen-fail-status
        exit 22
    fi
}
case "$1" in
    open)
        # Count how many times the connection is (re-)opened.
        read i < retry-reopen-fail-open-count
        ((i++))
        echo $i > retry-reopen-fail-open-count
        if [ $i -eq 2 ]; then
            echo "EIO open failed" >&2
            exit 1
        fi
        echo "handle"
        ;;
    pread)
        check_handle
        # Fail 2 times then succeed.
        read i < retry-reopen-fail-count
        ((i++))
        echo $i > retry-reopen-fail-count
        if [ $i -le 2 ]; then
            echo "EIO pread failed" >&2
            exit 1
        else
            dd if=/dev/zero count=$3 iflag=count_bytes
        fi
        ;;

    get_size)
        check_handle
        echo 512 ;;
    *) exit 2 ;;
esac
EOF

    # Check that running time appears reasonable.
    end_t=$SECONDS
    if [ $((end_t - start_t)) -lt $mintime ]; then
        echo "$0: test ran too quickly"
        fail=1
    fi

    # Check the handle was opened as often as expected.
    read open_count < retry-reopen-fail-open-count
    if [ $open_count -ne $expcount ]; then
        echo "$0: open-count ($open_count) != $expcount"
        fail=1
    fi

    # Check the exit status of qemu-io
    read qemu_status < retry-reopen-fail-status
    if [ $qemu_status -ne $status ]; then
        echo "$0: status ($qemu_status) != $status"
        fail=1
    fi
}

# In this first test we should see 3 failures:
# first pread FAILS
# retry and wait 1 seconds
# open FAILS
# retry and wait 2 seconds
# open succeeds
# first pread FAILS
# retry and wait 4 seconds
# open succeeds
# first pread succeeds
# second pread succeeds

# The minimum time for the test should be 1+2+4 = 7 seconds.
do_test 5 7 4 0

# In this second test we should see the following:
# first pread FAILS
# retry and wait 1 seconds
# open FAILS, ending first pread in failure
# first pread FAILS
# second pread requires open
# open succeeds
# second pread FAILS
# retry and wait 1 seconds
# open succeeds
# second pread succeeds

# The minimum time for the test should be 1+1 = 2 seconds.
do_test 1 2 3 1

exit $fail
