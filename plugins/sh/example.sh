#!/usr/bin/env bash
#
# Example Bash plugin.
#
# This example can be freely used for any purpose.
#
# Run it from the build directory like this:
#
#   ./nbdkit -f -v sh ./plugins/sh/example.sh file=disk.img
#
# Or run it after installing nbdkit like this:
#
#   nbdkit -f -v sh ./plugins/sh/example.sh file=disk.img
#
# The -f -v arguments are optional.  They cause the server to stay in
# the foreground and print debugging, which is useful when testing.
#
# You can connect to the server using guestfish or qemu, eg:
#
#   guestfish --format=raw -a nbd://localhost
#   ><fs> run
#   ><fs> list-filesystems
#   ><fs> mount /dev/sda1 /

# $tmpdir is set to a random, empty directory by nbdkit.  Note
# the contents are deleted when nbdkit exits.

# We make a symlink to the file in the tmpdir directory.
f=$tmpdir/file

case "$1" in
    dump_plugin)
        # This is called from: nbdkit sh example.sh --dump-plugin
        echo "example_sh=1"
        ;;

    config)
        # We expect a file=... parameter pointing to the file to serve.
        if [ "$2" = "file" ]; then
            if [ ! -r "$3" ]; then
                echo "file $3 does not exist or is not readable" >&2
                exit 1
            fi
            ln -sf `realpath "$3"` $f
        else
            echo "unknown parameter $2=$3" >&2
            exit 1
        fi
        ;;

    config_complete)
        # Check the file parameter was passed.
        if [ ! -L $f ]; then
            echo "file parameter missing" >&2
            exit 1
        fi
        ;;

    open)
        # Open a new client connection.
        #
        # If you want to keep per-handle state then create a temporary
        # directory under $tmpdir using the command below (note the
        # X's are literal).  This prints the new directory name on
        # stdout, which is passed back as the handle parameter ($2) in
        # calls below.  You can use this directory to store per-handle
        # state, and it is cleaned up by nbdkit on exit.
        #mktemp -d $tmpdir/handle-XXXXXX
        #
        # This example doesn't need per-handle state so we can
        # return anything:
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
        # If we provide a pwrite method, we must provide this method
        # (and similarly for flush and trim).  See nbdkit-sh-plugin(3)
        # for details.  This will exit 0 (below) which means true.
        # Use ‘exit 3’ if false.
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
