#!/usr/bin/env bash
#
# Example plugin for nbdkit which assembles a boot sector (using NASM)
# and can then be used to boot it in qemu.  This comes from a
# lightning talk I did at the KVM Forum in 2019.
#
# This example can be freely used for any purpose.
#
# Run it from the build directory like this:
#
#   ./nbdkit sh ./plugins/sh/assemble.sh input.asm \
#       --run 'qemu-system-i386 -hda $nbd'
#
# For some assembler files to use as input, see
# http://git.annexia.org/?p=libguestfs-talks.git;a=tree;f=2019-lightning-talk

# Check we're being run from nbdkit.
if [ ! -d $tmpdir ] || [ "x$1" = "x" ]; then
    echo "$0: this script must be run from nbdkit" >&2
    echo "Use ‘nbdkit sh $0’" >&2
    exit 1
fi

i=$tmpdir/input.asm
s=$tmpdir/source.asm
b=$tmpdir/binary

case "$1" in
    config)
        if [ "$2" = "file" ]; then
            ln -sf "$(realpath "$3")" $i
        else
            echo "unknown parameter $2=$3" >&2
            exit 1
        fi
        ;;
    config_complete)
        # Turn the input asm fragment into a boot sector and assemble it.
        echo 'org 07c00h'             > $s
        cat $i                       >> $s
        echo 'times 510-($-$$) db 0' >> $s
        echo 'db 055h,0aah'          >> $s
        nasm -f bin $s -o $b
        ;;
    get_size)
        echo 512
        ;;
    pread)
        dd if=$b skip=$4 count=$3 iflag=count_bytes,skip_bytes
        ;;
    can_write)
        # Default is yes to make it writable (but writes will fail).
        ;;
    pwrite)
        exit 1
        ;;
    magic_config_key)
        echo "file"
        ;;
    *)
        exit 2
        ;;
esac
