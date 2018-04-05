# Example Python plugin.
#
# This example can be freely used for any purpose.

# Run it from the build directory like this:
#
#   ./nbdkit -f -v python ./plugins/python/example.py test1=foo test2=bar
#
# Or run it after installing nbdkit like this:
#
#   nbdkit -f -v python ./plugins/python/example.py test1=foo test2=bar
#
# The -f -v arguments are optional.  They cause the server to stay in
# the foreground and print debugging, which is useful when testing.
#
# You can connect to the server using guestfish or qemu, eg:
#
#   guestfish --format=raw -a nbd://localhost
#   ><fs> run
#   ><fs> part-disk /dev/sda mbr
#   ><fs> mkfs ext2 /dev/sda1
#   ><fs> list-filesystems
#   ><fs> mount /dev/sda1 /
#   ><fs> [etc]

import nbdkit
import errno

# This is the string used to store the emulated disk (initially all
# zero bytes).  There is one disk per nbdkit instance, so if you
# reconnect to the same server you should see the same disk.  You
# could also put this into the handle, so there would be a fresh disk
# per handle.
disk = bytearray(1024 * 1024)


# This just prints the extra command line parameters, but real plugins
# should parse them and reject any unknown parameters.
def config(key, value):
    print("ignored parameter %s=%s" % (key, value))


def open(readonly):
    print("open: readonly=%d" % readonly)

    # You can return any non-NULL Python object from open, and the
    # same object will be passed as the first arg to the other
    # callbacks [in the client connected phase].
    return 1


def get_size(h):
    global disk
    return len(disk)


def pread(h, count, offset):
    global disk
    return disk[offset:offset+count]


def pwrite(h, buf, offset):
    global disk
    end = offset + len(buf)
    disk[offset:end] = buf


def zero(h, count, offset, may_trim):
    global disk
    if may_trim:
        disk[offset:offset+count] = bytearray(count)
    else:
        nbdkit.set_error(errno.EOPNOTSUPP)
        raise Exception
