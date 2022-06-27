#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2017-2020 Red Hat Inc.
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

# Check file-data was created by Makefile and qemu-io exists.
requires test -f file-data
requires qemu-io --version
requires timeout 60s true
requires dd iflag=count_bytes </dev/null

nbdkit --dump-plugin sh | grep -q ^thread_model=parallel ||
    { echo "nbdkit lacks support for parallel requests"; exit 77; }

# debuginfod breaks valgrinding of this test because it creates about
# a dozen pipe file descriptors, which breaks the leaked fd
# assumptions in the test below.
if [ "$NBDKIT_VALGRIND" = "1" ]; then
    requires test -z "$DEBUGINFOD_URLS"
fi

cleanup_fn rm -f test-parallel-sh.data test-parallel-sh.out test-parallel-sh.script

# Populate file, and sanity check that qemu-io can issue parallel requests
printf '%1024s' . > test-parallel-sh.data
timeout 30s </dev/null qemu-io -f raw -c "aio_write -P 1 0 512" \
        -c "aio_write -P 2 512 512" -c aio_flush test-parallel-sh.data ||
    { echo "'qemu-io' can't drive parallel requests"; exit 77; }

# Set up the sh plugin to delay both reads and writes (for a good
# chance that parallel requests are in flight), and with writes longer
# than reads (to more easily detect if out-of-order completion
# happens). This test may have spurious failures under heavy loads on
# the test machine, where tuning the delays may help.

# Also test for leaked fds when possible: nbdkit does not close fds it
# inherited, but other than 0, 1, 2, and the fd associated with the
# script, the child shell should not see any new fds not also present
# in nbdkit's parent environment.  When testing for the count of open
# fds, use ls in a subshell (rather than a glob directly within the
# shell under test), to avoid yet another fd open on the /proc/self/fd
# directory.

curr_fds=
if test -d /proc/$$/fd; then
    echo "parent fds:" >&2
    ls -l /proc/$$/fd >&2
    curr_fds=$(/usr/bin/env bash -c '(ls /proc/$$/fd)' | wc -w)
fi
export curr_fds
echo "using curr_fds=$curr_fds"

cat > test-parallel-sh.script <<'EOF'
#!/usr/bin/env bash
f=test-parallel-sh.data
if ! test -f $f; then
  echo "can't locate test-parallel-sh.data" >&2; exit 5
fi
if test -n "$curr_fds"; then
  (
    if test $( ls /proc/$$/fd | wc -w ) -ne $(($curr_fds + 1)); then
      echo "nbdkit script fds:" >&2
      ls -l /proc/$$/fd >&2
      echo "there seem to be leaked fds, curr_fds=$curr_fds" >&2
      exit 1
    fi
  ) || exit 5
fi
case $1 in
  thread_model) echo parallel ;;
  get_size) stat -L -c %s $f || exit 1 ;;
  pread) dd iflag=skip_bytes,count_bytes skip=$4 count=$3 if=$f || exit 1 ;;
  pwrite) dd oflag=seek_bytes conv=notrunc seek=$4 of=$f || exit 1 ;;
  can_write) ;;
  *) exit 2 ;;
esac
exit 0
EOF
chmod +x test-parallel-sh.script

# With --threads=1, the write should complete first because it was issued first
nbdkit -v -t 1 -U - --filter=delay sh test-parallel-sh.script \
  wdelay=2 rdelay=1 --run 'timeout 60s </dev/null qemu-io -f raw \
    -c "aio_write -P 2 512 512" -c "aio_read -P 1 0 512" -c aio_flush $nbd' |
    tee test-parallel-sh.out
if test "$(grep '512/512' test-parallel-sh.out)" != \
"wrote 512/512 bytes at offset 512
read 512/512 bytes at offset 0"; then
  exit 1
fi

# With default --threads, the faster read should complete first
nbdkit -v -U - --filter=delay sh test-parallel-sh.script \
  wdelay=2 rdelay=1 --run 'timeout 60s </dev/null qemu-io -f raw \
    -c "aio_write -P 2 512 512" -c "aio_read -P 1 0 512" -c aio_flush $nbd' |
    tee test-parallel-sh.out
if test "$(grep '512/512' test-parallel-sh.out)" != \
"read 512/512 bytes at offset 0
wrote 512/512 bytes at offset 512"; then
  exit 1
fi

# With --filter=noparallel, the write should complete first because it was
# issued first. Also test that the log filter doesn't leak an fd
nbdkit -v -U - --filter=noparallel --filter=log --filter=delay \
  sh test-parallel-sh.script logfile=/dev/null \
  wdelay=2 rdelay=1 --run 'timeout 60s </dev/null qemu-io -f raw \
    -c "aio_write -P 2 512 512" -c "aio_read -P 1 0 512" -c aio_flush $nbd' |
    tee test-parallel-sh.out
if test "$(grep '512/512' test-parallel-sh.out)" != \
"wrote 512/512 bytes at offset 512
read 512/512 bytes at offset 0"; then
  exit 1
fi

exit 0
