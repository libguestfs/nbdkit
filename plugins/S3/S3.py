#!@sbindir@/nbdkit python
# -*- python -*-
# Copyright (C) 2020 Red Hat Inc.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution.
#
# * Neither the name of Red Hat nor the names of its contributors may be
# used to endorse or promote products derived from this software without
# specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
# PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
# USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
# OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

import nbdkit
import boto3
import os
import unittest
from contextlib import closing
from botocore.exceptions import ClientError


API_VERSION = 2

access_key = None
secret_key = None
session_token = None
endpoint_url = None
bucket_name = None
key_name = None
dev_size = None
obj_size = None


def thread_model():
    return nbdkit.THREAD_MODEL_PARALLEL


def can_write(s3):
    return obj_size is not None


def can_multi_conn(s3):
    return True


def can_trim(s3):
    return False


def can_zero(s3):
    return False


def can_fast_zero(s3):
    return False


def can_cache(s3):
    return nbdkit.CACHE_NONE


def block_size(s3):
    if not obj_size:
        # More or less arbitrary.
        return (1, 512*1024, 0xffffffff)

    # Should return (minimum, preferred, maximum) block size. We return the
    # same value for all three because even though the plugin can handle
    # arbitrary large and small blocks, the performance penalty is huge and it
    # is always preferable for the client to split up requests as needed.
    return (obj_size, obj_size, obj_size)


def is_rotational(s3):
    return False


def can_fua(s3):
    return nbdkit.FUA_NATIVE


def can_extents(s3):
    return False


def can_flush(s3):
    return True


def config(key, value):
    global access_key, secret_key, session_token, endpoint_url, \
        bucket_name, key_name, dev_size, obj_size

    if key == "access-key" or key == "access_key":
        access_key = value
    elif key == "secret-key" or key == "secret_key":
        secret_key = value
    elif key == "session-token" or key == "session_token":
        session_token = value
    elif key == "endpoint-url" or key == "endpoint_url":
        endpoint_url = value
    elif key == "bucket":
        bucket_name = value
    elif key == "key":
        key_name = value
    elif key == 'size':
        dev_size = nbdkit.parse_size(value)
    elif key == 'object-size':
        obj_size = nbdkit.parse_size(value)
    else:
        raise RuntimeError("unknown parameter %s" % key)


def config_complete():
    if bucket_name is None:
        raise RuntimeError("bucket parameter missing")
    if key_name is None:
        raise RuntimeError("key parameter missing")

    if (dev_size and not obj_size or
            obj_size and not dev_size):
        raise RuntimeError(
            "`size` and `object-size` parameters must always be "
            "specified together")


def open(readonly):
    s3 = boto3.client("s3",
                      aws_access_key_id=access_key,
                      aws_secret_access_key=secret_key,
                      aws_session_token=session_token,
                      endpoint_url=endpoint_url)
    return s3


def get_size(s3):
    if dev_size:
        return dev_size

    try:
        resp = s3.head_object(Bucket=bucket_name, Key=key_name)
    except AttributeError:
        resp = s3.get_object(Bucket=bucket_name, Key=key_name)

    size = resp['ResponseMetadata']['HTTPHeaders']['content-length']
    return int(size)


def pread(s3, buf, offset, flags):
    if obj_size:
        return pread_multi(s3, buf, offset, flags)

    size = len(buf)
    buf[:] = get_object(s3, key_name, size=size, off=offset)


def pwrite(s3, buf, offset, flags):
    # We can ignore FUA flags, because every write is always flushed
    if obj_size:
        return pwrite_multi(s3, buf, offset, flags)

    raise RuntimeError('Unable to write in single-object mode')


def flush(s3, flags):
    # Flush is implicitly done on every request.
    return


def pread_multi(s3, buf, offset, flags):
    "Read data from objects"

    to_read = len(buf)
    read = 0

    (blockno, block_offset) = divmod(offset, obj_size)
    while to_read > 0:
        key = f"{key_name}/{blockno:016x}"
        len_ = min(to_read, obj_size - block_offset)
        buf[read:read + len_] = get_object(
            s3, key, size=len_, off=block_offset
        )
        to_read -= len_
        read += len_
        blockno += 1
        block_offset = 0


def get_object(s3, obj_name, size, off):
    """Read *size* bytes from *obj_name*, starting at *off*."""

    try:
        resp = s3.get_object(Bucket=bucket_name, Key=obj_name,
                             Range=f'bytes={off}-{off+size-1}')
    except ClientError as exc:
        if exc.response['Error']['Code'] == 'NoSuchKey':
            return bytearray(size)
        raise

    body = resp['Body']
    with closing(body):
        buf = body.read()

    assert len(buf) == size, f'requested {size} bytes, got {len(buf)}'
    return buf


def concat(b1, b2):
    """Concatenate two byte-like objects (including memoryviews)"""

    l1 = len(b1)
    l3 = l1 + len(b2)
    b3 = bytearray(l3)
    b3[:l1] = b1
    b3[l1:] = b2
    return b3


def pwrite_multi(s3, buf, offset, flags):
    "Write data to objects"

    to_write = len(buf)

    # memoryviews can be sliced without copying the data
    if not isinstance(buf, memoryview):
        buf = memoryview(buf)

    (blockno1, block_offset) = divmod(offset, obj_size)
    if block_offset:
        nbdkit.debug(f"pwrite_multi(): write at offset {offset} not aligned, "
                     f"starts {block_offset} bytes after block {blockno1}. "
                     "Fetching preceding data...")
        key = f'{key_name}/{blockno1:016x}'
        pre = get_object(s3, key, size=block_offset, off=0)
        len_ = obj_size - block_offset

        if to_write + block_offset < obj_size:
            # Still no full object, need to append at the end
            buf = concat(pre, buf)
            to_write += block_offset
            offset -= block_offset
        else:
            nbdkit.debug(f"pwrite_multi(): writing block {blockno1}...")
            put_object(s3, key, pre + buf[:len_])
            buf = buf[len_:]
            to_write -= len_
            offset += len_

        (blockno1, block_offset) = divmod(offset, obj_size)
        assert block_offset == 0

    (blockno2, block_offset) = divmod(offset + to_write, obj_size)
    if block_offset:
        nbdkit.debug(f"pwrite_multi(): write at offset {offset} not aligned, "
                     f"ends {obj_size-block_offset} bytes before block "
                     f"{blockno2+1}. Fetching following data...")
        key = f'{key_name}/{blockno2:016x}'
        post = get_object(
            s3, key, size=obj_size-block_offset, off=block_offset)
        len_ = obj_size - block_offset
        post = get_object(s3, key, size=len_, off=block_offset)
        nbdkit.debug(f"pwrite_multi(): writing block {blockno2}...")
        put_object(s3, key, concat(buf[-block_offset:], post))
        buf = buf[:-block_offset]

    off = 0
    for blockno in range(blockno1, blockno2):
        nbdkit.debug(f"pwrite_multi(): writing block {blockno}...")
        key = f"{key_name}/{blockno:016x}"
        put_object(s3, key, buf[off:off + obj_size])
        off += obj_size


def put_object(s3, obj_name, buf):
    "Write *buf* into *obj_name"

    assert len(buf) == obj_size

    # Boto does not support reading from memoryviews :-(
    if isinstance(buf, memoryview):
        buf = buf.tobytes()
    s3.put_object(Bucket=bucket_name, Key=obj_name, Body=buf)


#
# To run unit tests, set the TEST_BUCKET, AWS_ACCESS_KEY_ID,
# and AWS_SECRET_ACCESS_KEY environment variables and execute
# `python3 -m unittest S3.py`
#
@unittest.skipIf('TEST_BUCKET' not in os.environ,
                 'TEST_BUCKET environment variable not defined.')
class Test(unittest.TestCase):
    def setUp(self):
        self.s3 = open(False)
        self.obj_size = 64
        config('bucket', os.environ['TEST_BUCKET'])
        config('key', 'nbdkit_test')
        config('object-size', str(self.obj_size))
        config('size', str(self.obj_size*100))

    def test_read_hole(self):
        self.s3.delete_object(Bucket=bucket_name, Key=f"{key_name}/{5:016x}")
        buf = bytearray(self.obj_size)
        pread(self.s3, buf, 5*self.obj_size, 0)
        self.assertEqual(buf, bytearray(self.obj_size))

    def test_readwrite(self):
        buf1 = bytearray(b'x' * self.obj_size)
        pwrite(self.s3, buf1, self.obj_size, 0)
        buf2 = bytearray(self.obj_size)
        pread(self.s3, buf2, self.obj_size, 0)
        self.assertEqual(buf1, buf2)

        buf1 = bytearray(b'y' * self.obj_size)
        pwrite(self.s3, buf1, self.obj_size, 0)
        buf2 = bytearray(self.obj_size)
        pread(self.s3, buf2, self.obj_size, 0)
        self.assertEqual(buf1, buf2)

    def test_partial_read(self):
        buf1 = bytearray(b'x' * self.obj_size)
        buf2 = bytearray(b'y' * self.obj_size)
        pwrite(self.s3, buf1, 0, 0)
        pwrite(self.s3, buf2, self.obj_size, 0)

        hl = self.obj_size//2
        buf = bytearray(self.obj_size)
        pread(self.s3, buf, hl, 0)

        self.assertEqual(buf[:hl], buf1[hl:])
        self.assertEqual(buf[hl:], buf2[:hl])

    def test_partial_write(self):
        buf1 = bytearray(b'x' * self.obj_size)
        pwrite(self.s3, buf1, 0, 0)

        hl = self.obj_size//4
        buf2 = bytearray(b'y' * hl)
        pwrite(self.s3, buf2, hl, 0)
        buf1[hl:hl+len(buf2)] = buf2

        buf = bytearray(self.obj_size)
        pread(self.s3, buf, 0, 0)
        self.assertEqual(buf1, buf)

    def test_write_memoryview(self):
        buf = memoryview(bytearray(obj_size))
        pwrite(self.s3, buf, 0, 0)
        pwrite(self.s3, buf[10:], 42, 0)

    def test_read_multi(self):
        b1 = b'x' * self.obj_size
        b2 = b'y' * self.obj_size
        b3 = b'z' * self.obj_size
        pwrite(self.s3, b1, 0, flags=0)
        pwrite(self.s3, b2, self.obj_size, flags=0)
        pwrite(self.s3, b3, 2*self.obj_size, flags=0)

        buf = bytearray(3*self.obj_size)
        pread(self.s3, buf, 0, flags=0)

        self.assertEqual(buf, b1+b2+b3)

    def test_write_multi(self):
        buf = (b'x' * self.obj_size
               + b'y' * self.obj_size
               + b'z' * self.obj_size)
        pwrite(self.s3, buf, 0, flags=0)

        b1 = bytearray(self.obj_size)
        pread(self.s3, b1, 0, flags=0)
        b2 = bytearray(self.obj_size)
        pread(self.s3, b2, self.obj_size, flags=0)
        b3 = bytearray(self.obj_size)
        pread(self.s3, b3, 2*self.obj_size, flags=0)

        self.assertEqual(buf, b1+b2+b3)
