# Example Python plugin.
#
# This example can be freely used for any purpose.

# Run it from the build directory like this:
#
#   ./nbdkit -f -v python ./plugins/python/examples/file.py file=test.img
#
# Or run it after installing nbdkit like this:
#
#   nbdkit -f -v python ./plugins/python/examples/file.py file=test.img
#
# The -f -v arguments are optional.  They cause the server to stay in
# the foreground and print debugging, which is useful when testing.

import os

import nbdkit

# There are several variants of the API.  nbdkit will use this
# constant to determine which one you want to use.  This is the latest
# version at the time this example was written.
API_VERSION = 2

# The file we want to serve.
filename = None


# Parse the file parameter which contains the name of the file that we
# want to serve.
def config(key, value):
    global filename
    if key == "file":
        filename = os.path.abspath(value)
    else:
        raise RuntimeError("unknown parameter: " + key)


def config_complete():
    global filename
    if filename is None:
        raise RuntimeError("file parameter is required")


# Although Python code cannot be run in parallel, if your
# plugin callbacks sleep then you can improve parallelism
# by relaxing the thread model.
def thread_model():
    return nbdkit.THREAD_MODEL_PARALLEL


# This is called when a client connects.
def open(readonly):
    if readonly:
        flags = os.O_RDONLY
    else:
        flags = os.O_RDWR
    fd = os.open(filename, flags)
    return {'fd': fd}


def get_size(h):
    sb = os.stat(h['fd'])
    return sb.st_size


def pread(h, buf, offset, flags):
    n = os.preadv(h['fd'], [buf], offset)
    if n != len(buf):
        raise RuntimeError("unexpected short read from file")


def pwrite(h, buf, offset, flags):
    n = os.pwritev(h['fd'], [buf], offset)
    if n != len(buf):
        raise RuntimeError("short write")
