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

# Note that the exit code of the script matters:
#  0 => OK
#  1 => Error
#  2 => Method is missing
#  3 => False
# For other values, see the nbdkit-sh-plugin(3) manual page.

# Check we're being run from nbdkit.
#
# Because the script has to be executable (for nbdkit to run it) there
# is a danger that someone could run the script standalone which won't
# work.  Use two tests to try to make sure we are run from nbdkit:
#
# - $tmpdir is set to a random, empty directory by nbdkit.  Note the
# contents are deleted when nbdkit exits.
#
# - $1 is set (to a method name).
if [ ! -d $tmpdir ] || [ "x$1" = "x" ]; then
    echo "$0: this script must be run from nbdkit" >&2
    echo "Use ‘nbdkit sh $0’" >&2
    exit 1
fi

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
            ln -sf "$(realpath "$3")" $f
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

    thread_model)
        # You must opt-in for parallel behavior; the default is
        # serialize_all_requests.
        echo parallel
        ;;

    open)
        # Open a new client connection.

        # Create a directory to store per-connection state.  The
        # directory name is printed out by the mktemp command, that
        # output is captured by nbdkit, and it is passed back as the
        # handle parameter ($2) in subsequent calls below.
        #
        # You can use this directory to store per-connection state,
        # and it along with everything in $tmpdir is cleaned up by
        # nbdkit on exit.
        #
        # (This plugin does not actually use per-connection state,
        # it's just an example.)
        mktemp -d $tmpdir/handle-XXXXXX
        ;;

    get_size)
        # Print the disk size on stdout.
        stat -L -c '%s' $f || exit 1
        ;;

    pread)
        # Read the requested part of the disk and write to stdout.
        dd iflag=skip_bytes,count_bytes skip=$4 count=$3 if=$f || exit 1
        ;;

    pwrite)
        # Copy data from stdin and write it to the disk.
        dd oflag=seek_bytes conv=notrunc seek=$4 of=$f || exit 1
        ;;

    can_write)
        # If we provide a pwrite method, we must provide this method
        # (and similarly for flush and trim).  See nbdkit-sh-plugin(3)
        # for details.  This will exit 0 (below) which means true.
        # Use ‘exit 3’ if false.
        ;;

    trim)
        # Punch a hole in the backing file, if supported.
        fallocate -p -o $4 -l $3 -n $f || exit 1
        ;;
    can_trim)
        # We can trim if the fallocate command exists.
        fallocate --help >/dev/null 2>&1 || exit 3
        ;;

    zero)
        # Efficiently zero the backing file, if supported.
        # Try punching a hole if flags includes may_trim, otherwise
        # request to leave the zeroed range allocated.
        # Attempt a fallback to write on any failure, but this requires
        # specific prefix on stderr prior to any message from fallocate;
        # exploit the fact that stderr is ignored on success.
        echo ENOTSUP >&2
        case ,$5, in
            *,may_trim,*) fallocate -p -o $4 -l $3 -n $f || exit 1 ;;
            *)            fallocate -z -o $4 -l $3 -n $f || exit 1 ;;
        esac
        ;;
    can_zero)
        # We can efficiently zero if the fallocate command exists.
        fallocate --help >/dev/null 2>&1 || exit 3
        ;;

    # cache)
        # Implement an efficient prefetch, if desired.
        # It is intentionally omitted from this example.
        # dd iflag=skip_bytes,count_bytes skip=$4 count=$3 \
        #    if=$f of=/dev/null || exit 1
        # ;;

    can_cache)
        # Caching is not advertised to the client unless can_cache prints
        # a tri-state value.  Here, we choose for caching to be a no-op,
        # by omitting counterpart handling for 'cache'.
        echo native
        ;;

    extents)
        # Report extent (block status) information as 'offset length [type]'.
        # This example could omit the handler, since it just matches
        # the default behavior of treating everything as data; but if
        # your code can detect holes, this demonstrates the usage.
        echo "$4           $(($3/2)) 0"
        echo "$(($4+$3/2)) $(($3/2))"
        # echo "$4 $3 hole,zero"
        ;;

    can_extents)
        # Similar to can_write
        ;;

    *)
        # Unknown methods must exit with code 2.
        exit 2
esac

exit 0
