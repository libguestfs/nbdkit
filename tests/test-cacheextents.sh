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
set -x
set -e

requires_plugin sh
requires_filter cacheextents
requires grep --version
requires qemu-io --version
requires dd iflag=count_bytes </dev/null

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
sockurl="nbd+unix:///?socket=$sock"
pidfile="test-cacheextents.pid"
accessfile="test-cacheextents-access.log"
accessfile_full="$PWD/test-cacheextents-access.log"
files="$pidfile $sock"
rm -f $files $accessfile
cleanup_fn rm -f $files

export accessfile_full
start_nbdkit \
    -P $pidfile \
    -U $sock \
    --filter=cacheextents \
    sh - <<'EOF'
echo "Call: $@" >>$accessfile_full
size=4M
block_size=$((1024*1024))
case "$1" in
  thread_model) echo parallel ;;
  get_size) echo $size ;;
  can_extents) ;;
  extents)
    echo "extents request: $@" >>$accessfile_full
    offset=$(($4 / $block_size))
    count=$(($3 / $block_size))
    length=$(($offset + $count))
    for i in $(seq $offset $length); do
      echo ${i}M $block_size $((i%4)) >>$accessfile_full
      echo ${i}M $block_size $((i%4))
    done
    ;;
  pread) dd if=/dev/zero count=$3 iflag=count_bytes ;;
  can_write) ;;
  pwrite) dd of=/dev/null ;;
  can_trim) ;;
  trim) ;;
  can_zero) ;;
  zero) ;;
  *) exit 2 ;;
esac
EOF


test_me() {
    num_accesses=$1
    shift

    qemu-io -f raw "$@" "$sockurl"
    test "$(grep -c "^extents request: " $accessfile)" -eq "$num_accesses"
    ret=$?
    rm -f "$accessfile"
    return $ret
}

# First one causes caching, the rest should be returned from cache.
test_me 1 -c 'map' -c 'map' -c 'map'
# First one is still cached from last time, discard should kill the cache, then
# one request should go through.
test_me 1 -c 'map' -c 'discard 0 1' -c 'map'
# Same as above, only this time the cache is killed before all the operations as
# well.  This is used from now on to clear the cache as it seems nicer and
# faster than running new nbdkit for each test.
test_me 2 -c 'discard 0 1' -c 'map' -c 'discard 0 1' -c 'map'
# Write should kill the cache as well.
test_me 2 -c 'discard 0 1' -c 'map' -c 'write 0 1' -c 'map'
# Alloc should use cached data from map
test_me 1 -c 'discard 0 1' -c 'map' -c 'alloc 0'
# Read should not kill the cache
test_me 1 -c 'discard 0 1' -c 'map' -c 'read 0 1' -c 'map' -c 'alloc 0'
