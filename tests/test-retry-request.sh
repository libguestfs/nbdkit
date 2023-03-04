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

source ./functions.sh
set -e
set -x

requires_plugin sh
requires_nbdcopy
requires dd iflag=count_bytes </dev/null

files="retry-request.img retry-request-count"
rm -f $files
cleanup_fn rm -f $files

touch retry-request-count
start_t=$SECONDS

# Create a custom plugin which will test retrying requests.
nbdkit -v -U - \
       sh - \
       --filter=retry-request retry-request-retries=3 retry-request-delay=1 \
       --run 'nbdcopy --synchronous "$uri" retry-request.img' <<'EOF'
#!/usr/bin/env bash
case "$1" in
    get_size) echo 512 ;;
    pread)
        # Fail 3 times then succeed.
        read i < retry-request-count
        ((i++))
        echo $i > retry-request-count
        if [ $i -le 3 ]; then
            echo "EIO pread failed" >&2
            exit 1
        else
            dd if=/dev/zero count=$3 iflag=count_bytes
        fi
        ;;

    *) exit 2 ;;
esac
EOF

# In this test we should see 3 failures:
# pread FAILS
# retry and wait 1 second
# pread FAILS
# retry and wait 1 second
# pread FAILS
# retry and wait 1 second
# pread succeeds

# The minimum time for the test should be 3 seconds.
end_t=$SECONDS
if [ $((end_t - start_t)) -lt 3 ]; then
    echo "$0: test ran too quickly"
    exit 1
fi

# Check retry-request-count = 4 (because we write #retries+1 to the file)
read retry_count < retry-request-count
if [ $retry_count -ne 4 ]; then
    echo "$0: retry-request-count ($retry_count) != 4"
    exit 1
fi
