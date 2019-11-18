#!/usr/bin/env bash
#
# Example plugin for nbdkit which lets you write a shell script plugin
# entirely on the nbdkit command line.
#
# This example can be freely used for any purpose.
#
# Run it from the build directory like this:
#
#   ./nbdkit sh ./plugins/sh/eval.sh \
#     get_size='echo 64M' \
#     pread='dd if=/dev/zero count=$3 iflag=count_bytes' \
#     pwrite='echo ENOSPC Out of space >&2; exit 1'
#
# which creates a 64M disk of zeroes that gives an ENOSPC error when
# written to.

# Check we're being run from nbdkit.
if [ ! -d $tmpdir ] || [ "x$1" = "x" ]; then
    echo "$0: this script must be run from nbdkit" >&2
    echo "Use ‘nbdkit sh $0’" >&2
    exit 1
fi

function create_can_wrapper
{
    if [ -f $tmpdir/$1 ] && [ ! -f $tmpdir/$2 ]; then
        echo 'exit 0' > $tmpdir/$2
        chmod +x $tmpdir/$2
    fi
}

case "$1" in
    config)
        # Save each parameter to a separate file under $tmpdir.
        # Security is a thing.  We're relying on the fact that
        # parameter keys in nbdkit are only allowed to use a limited
        # range of characters.
        echo "$3" > $tmpdir/$2
        chmod +x $tmpdir/$2
        ;;
    config_complete)
        # For convenience create can_* wrappers corresponding to
        # certain scripts, but only if they don't exist already.
        create_can_wrapper pwrite  can_write
        create_can_wrapper flush   can_flush
        create_can_wrapper trim    can_trim
        create_can_wrapper zero    can_zero
        create_can_wrapper extents can_extents
        ;;

    *)
        # If the script corresponding to this method exists, run it,
        # otherwise exit with 2 (missing method).
        if [ -x $tmpdir/$1 ]; then
            exec $tmpdir/$1 "$@"
        else
            exit 2
        fi
        ;;
esac
