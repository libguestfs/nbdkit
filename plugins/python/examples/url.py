# Example Python plugin.
#
# This example can be freely used for any purpose.

# Run it from the build directory like this:
#
#   ./nbdkit -f -v python ./plugins/python/examples/url.py \
#       url=http://example.com/disk.img
#
# Or run it after installing nbdkit like this:
#
#   nbdkit -f -v python ./plugins/python/examples/url.py \
#       url=http://example.com/disk.img
#
# The -f -v arguments are optional.  They cause the server to stay in
# the foreground and print debugging, which is useful when testing.

import urllib.request

import nbdkit

# There are several variants of the API.  nbdkit will use this
# constant to determine which one you want to use.  This is the latest
# version at the time this example was written.
API_VERSION = 2

url = None


# Parse the url parameter.
def config(key, value):
    global url
    if key == "url":
        url = value
    else:
        raise RuntimeError("unknown parameter: " + key)


def config_complete():
    global url
    if url is None:
        raise RuntimeError("url parameter is required")


# Although Python code cannot be run in parallel, if your
# plugin callbacks sleep then you can improve parallelism
# by relaxing the thread model.
def thread_model():
    return nbdkit.THREAD_MODEL_PARALLEL


# This is called when a client connects.
def open(readonly):
    return 1


def get_size(h):
    rq = urllib.request.Request(url, method='HEAD')
    rp = urllib.request.urlopen(rq)
    headers = rp.info()

    # Check the server supports range requests.
    if headers.get_all('accept-ranges') == []:
        raise RuntimeError("server does not support range requests")

    content_length = int(headers.get_all('content-length')[0])
    return content_length


def pread(h, buf, offset, flags):
    count = len(buf)
    headers = {"range": "bytes=%d-%d" % (offset, offset+count-1)}
    rq = urllib.request.Request(url, b"", headers)
    rp = urllib.request.urlopen(rq)
    b = rp.read()
    if len(b) != count:
        raise RuntimeError("incorrect length of data returned: perhaps the " +
                           "server does not really support range requests")
    buf[:] = b
