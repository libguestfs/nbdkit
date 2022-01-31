# Example Python plugin.
#
# This plugin simulates errors for testing NBD client error hanlding.
# Every odd call will fail, and every even call will succeed, unless
# there a real error accesing the specified file.
#
# This example can be freely used for any purpose.

# Run it from the build directory like this:
#
#   ./nbdkit -f -v python ./plugins/python/examples/error.py file=test.img
#
# Or run it after installing nbdkit like this:
#
#   nbdkit -f -v python ./plugins/python/examples/error.py file=test.img
#
# The -f -v arguments are optional.  They cause the server to stay in
# the foreground and print debugging, which is useful when testing.

import os
import nbdkit

API_VERSION = 2

filename = None
calls = 0


def config(key, value):
    global filename
    assert key == "file"
    filename = value


def thread_model():
    # Serialize all requests so we can seek safely in pread and pwrite
    # and be compatible with python 3.6. In python 3.7 we can use
    # os.preadv and os.pwritev and use the parallel threading model.
    return nbdkit.THREAD_MODEL_SERIALIZE_ALL_REQUESTS


def open(readonly):
    flags = os.O_RDONLY if readonly else os.O_RDWR
    return {"fd": os.open(filename, flags)}


def can_extents(h):
    return True


def get_size(h):
    return os.stat(h["fd"]).st_size


def extents(h, count, offset, flags):
    global calls
    calls += 1
    if calls % 2:
        raise RuntimeError(f"extents error offset={offset} count={count}")

    # We don't really support extents, so we report the entire file as
    # data.
    return [(offset, count, 0)]


def pread(h, buf, offset, flags):
    global calls
    calls += 1
    if calls % 2:
        raise RuntimeError(f"pread error offset={offset} count={len(buf)}")

    os.lseek(h["fd"], offset, os.SEEK_SET)
    n = os.readv(h['fd'], [buf])
    assert n == len(buf)


def pwrite(h, buf, offset, flags):
    global calls
    calls += 1
    if calls % 2:
        raise RuntimeError(f"pwrite error offset={offset} count={len(buf)}")

    os.lseek(h["fd"], offset, os.SEEK_SET)
    n = os.writev(h['fd'], [buf])
    assert n == len(buf)
