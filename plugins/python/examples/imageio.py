# Example Python plugin.
#
# This example can be freely used for any purpose.
#
# Upload and download images to oVirt with nbdkit and qemu-img.
#
# This assumes you have an oVirt environment. If you don't please see
# https://ovirt.org/ for info on how to install oVirt.
#
# Install ovirt-release package:
#
#  dnf install http://resources.ovirt.org/pub/yum-repo/ovirt-release-master.rpm
#
# Install required packages:
#
#   dnf install ovirt-imageio-client python3-ovirt-engine-sdk4
#
# Note: python3-ovirt-engine-sdk4 is not available yet for Fedora 31 and 32.
#
# To upload or download images, you need to start an image transfer. The
# easiest way is using oVirt image_transfer.py example:
#
#  /usr/share/doc/python3-ovirt-engine-sdk4/examples/image_transfer.py \
#      --engine-url https://my.engine \
#      --username admin@internal \
#      --password-file password \
#      --cafile my.engine.pem \
#      upload disk-uuid
#
# This will print the transfer URL for this image transfer.
#
# Run this example from the build directory:
#
#   ./nbdkit -t4 -f -v -U /tmp/nbd.sock python \
#       ./plugins/python/examples/imageio.py \
#       transfer_url=https://server:54322/images/ticket-id \
#       connections=4 \
#       secure=no
#
# Note that number of nbdkit threads and imageio connections should match.
#
# To upload an image run:
#
#   qemu-img convert -n -f raw -O raw -W disk.img \
#       nbd+unix:///\?socket=/tmp/nbd.sock
#
# Downloading image is not efficient with this version, since we don't report
# extents yet.
#
# The -f -v arguments are optional.  They cause the server to stay in
# the foreground and print debugging, which is useful when testing.

import queue
from contextlib import contextmanager

from ovirt_imageio.client import ImageioClient

import nbdkit

# Using version 2 supporting the buffer protocol for better performance.
API_VERSION = 2

# Plugin configuration, can be set using key=value in the command line.
params = {
    "secure": True,
    "ca_file": "",
    "connections": 1,
    "transfer_url": None,
}


def config(key, value):
    """
    Parse the url parameter which contains the transfer URL that we want to
    serve.
    """
    if key == "transfer_url":
        params["transfer_url"] = value
    elif key == "connections":
        params["connections"] = int(value)
    elif key == "ca_file":
        params["ca_file"] = value
    elif key == "secure":
        params["secure"] = boolify(key, value)
    else:
        raise RuntimeError("unknown parameter: {!r}".format(key))


def boolify(key, value):
    v = value.lower()
    if v in ("yes", "true", "1"):
        return True
    if v in ("no", "false", 0):
        return False
    raise RuntimeError("Invalid boolean value for {}: {!r}".format(key, value))


def config_complete():
    """
    Called when configuration is completed.
    """
    if params["transfer_url"] is None:
        raise RuntimeError("'transfer_url' parameter is required")


def thread_model():
    """
    Using parallel model to speed up transfer with multiple connections to
    imageio server.
    """
    return nbdkit.THREAD_MODEL_PARALLEL


def open(readonly):
    """
    Called once when plugin is loaded. We create a pool of connected clients
    that will be used for requests later.
    """
    pool = queue.Queue()
    for i in range(params["connections"]):
        client = ImageioClient(
            params["transfer_url"],
            cafile=params["ca_file"],
            secure=params["secure"])
        pool.put(client)
    return {"pool": pool}


def close(h):
    """
    Called when plugin is closed. Close and remove all clients from the pool.
    """
    pool = h["pool"]
    while not pool.empty():
        client = pool.get()
        client.close()


@contextmanager
def client(h):
    """
    Context manager fetching an imageio client from the pool. Blocks until a
    client is available.
    """
    pool = h["pool"]
    client = pool.get()
    try:
        yield client
    finally:
        pool.put(client)


def get_size(h):
    with client(h) as c:
        return c.size()


def pread(h, buf, offset, flags):
    with client(h) as c:
        c.read(offset, buf)


def pwrite(h, buf, offset, flags):
    with client(h) as c:
        c.write(offset, buf)


def zero(h, count, offset, flags):
    with client(h) as c:
        c.zero(offset, count)


def flush(h, flags):
    with client(h) as c:
        c.flush()
