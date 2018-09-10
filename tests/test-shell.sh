#!/usr/bin/env sh

# Assume that the current directory is the tests/ directory.
# If this fails we're probably running the test from the wrong
# directory.
f=test-shell.img
if [ ! -r $f ]; then
    echo "cannot find disk image $f" >&2
    exit 1
fi

# nbdkit is supposed to set $tmpdir.  If it doesn't, it's an error.
if [ ! -d $tmpdir ]; then
    echo "\$tmpdir was not set"
    exit 1
fi

case "$1" in
    open)
        echo handle
        ;;

    get_size)
        stat -L -c '%s' $f || exit 1
        ;;

    pread)
        dd iflag=skip_bytes,count_bytes skip=$4 count=$3 if=$f || \
            exit 1
        ;;

    pwrite)
        dd oflag=seek_bytes conv=notrunc seek=$4 of=$f || exit 1
        ;;
    can_write)
        ;;

    flush)
        sync
        ;;
    can_flush)
        ;;

    trim)
        fallocate -p -o $4 -l $3 -n $f
        ;;
    can_trim)
        # We can trim if the fallocate command exists.
        fallocate --help >/dev/null 2>&1 || exit 3
        ;;

    zero)
        fallocate -z -o $4 -l $3 -n $f
        ;;
    can_zero)
        # We can zero efficiently if the fallocate command exists.
        fallocate --help >/dev/null 2>&1 || exit 3
        ;;

    *)
        # Unknown methods must exit with code 2.
        exit 2
esac

exit 0
