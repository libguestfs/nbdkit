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
requires sfdisk --help
requires test -r /dev/urandom
requires qemu-img --version

# RHEL 7 sfdisk didn't have the -X option, so skip the tests here.
if LANG=C sfdisk -X |& grep -sq "invalid option"; then
    echo "$0: skipping test because no sfdisk -X option"
    exit 77
fi

d="partition1.d"
rm -rf $d
cleanup_fn rm -rf $d
mkdir $d

do_test ()
{
    label=$1
    nrparts=$2
    skip_extended=$3

    rm -f $d/disk
    truncate -s 1G $d/disk
    sfdisk -X $label $d/disk

    # Run nbdkit on each partition, copying data in and out.
    for ((part=1; part <= $nrparts; ++part)); do
        # The smallest partition in any test is 1023 sectors.  However
        # to make things quicker only write a sector of random data.
        dd if=/dev/urandom of=$d/rand bs=512 count=1

        if [ "$part" != "$skip_extended" ]; then
            nbdkit -f -v -U - \
                   --filter=partition file $d/disk partition=$part \
                   --run "qemu-img convert -n $d/rand \$nbd"
            nbdkit -f -v -U - \
                   --filter=partition file $d/disk partition=$part \
                   --run "qemu-img convert \$nbd $d/out"
            truncate -s 512 $d/out
            cmp $d/rand $d/out
        fi
    done
}

# Regular MBR with 1-4 primary partitions.
do_test dos 1 <<'EOF'
2048 1023 L -
EOF

do_test dos 2 <<'EOF'
2048 1023 L -
4096 4095 L -
EOF

do_test dos 3 <<'EOF'
2048 1023 L -
4096 4095 L -
8192 8191 L -
EOF

do_test dos 4 <<'EOF'
2048 1023 L -
4096 4095 L -
8192 8191 L -
16384 16383 L -
EOF

# MBR with 3 primary partitions and 2 logical partitions.
# Ignore partition 4 which is the extended partition.
do_test dos 6 4 <<'EOF'
2048 2047 L -
4096 4095 L -
8192 8191 L -
16384 16383 E -
17000 999 L -
18000 999 L -
EOF

# As above but the extended partition is 1.
do_test dos 6 1 <<'EOF'
16384 16383 E -
2048 2047 L -
4096 4095 L -
8192 8191 L -
17000 999 L -
18000 999 L -
EOF

# MBR also allows missing partitions.  This disk has only partition 2.
# (Partition 1 must be ignored since it is missing.)
do_test dos 2 1 <<'EOF'
disk2: start=1024 size=1023 type=83
EOF

# Regular GPT with 1-6 partitions.
do_test gpt 1 <<'EOF'
2048 1023 L -
EOF

do_test gpt 2 <<'EOF'
2048 1023 L -
4096 4095 L -
EOF

do_test gpt 3 <<'EOF'
2048 1023 L -
4096 4095 L -
8192 8191 L -
EOF

do_test gpt 4 <<'EOF'
2048 1023 L -
4096 4095 L -
8192 8191 L -
16384 16383 L -
EOF

do_test gpt 5 <<'EOF'
2048 2047 L -
4096 4095 L -
8192 8191 L -
16384 16383 L -
32768 32767 L -
EOF

do_test gpt 6 <<'EOF'
2048 2047 L -
4096 4095 L -
8192 8191 L -
16384 16383 L -
32768 32767 L -
65536 65535 L -
EOF
