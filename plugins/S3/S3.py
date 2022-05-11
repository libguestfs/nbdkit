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

import base64
import os
import re
import tempfile
import unittest
from contextlib import closing
from io import BytesIO

import boto3
import builtins
from botocore.exceptions import ClientError

import nbdkit

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

    if dev_size and dev_size % obj_size != 0:
        raise RuntimeError('`size` must be a multiple of `object-size`')


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


def delete_object(s3, obj_name, force=False):
    '''Delete *obj_name.

    If *force* is true, do not raise exception if object does not
    exist.
    '''

    try:
        s3.delete_object(Bucket=bucket_name, Key=obj_name)
    except ClientError as exc:
        if force and exc.response['Error']['Code'] == 'NoSuchKey':
            return
        raise


def make_client_error(errcode):
    return ClientError(
        error_response={'Error': {'Code': errcode}},
        operation_name='unspecified')


class MockS3Client:
    def __init__(self):
        self.keys = {}

    def put_object(self, Bucket: str, Key: str, Body):
        self.keys[(Bucket, Key)] = bytes(Body)

    def get_object(self, Bucket: str, Key: str, Range=None):
        key = (Bucket, Key)
        if key not in self.keys:
            raise make_client_error('NoSuchKey')

        if Range:
            hit = re.match(r'^bytes=(\d+)-(\d+)$', Range)
            assert hit
            (a, b) = [int(x) for x in hit.groups()]
            buf = self.keys[key][a:b+1]
        else:
            buf = self.keys[key]
        return {'Body': BytesIO(buf)}

    def delete_object(self, Bucket: str, Key: str):
        key = (Bucket, Key)
        if key not in self.keys:
            raise make_client_error('NoSuchKey')
        del self.keys[key]


class Test(unittest.TestCase):
    def setUp(self):
        super().setUp()
        self.s3 = MockS3Client()
        self.obj_size = 64
        self.dev_size = 100*self.obj_size
        self.ref_fh = tempfile.TemporaryFile()
        self.ref_fh.truncate(dev_size)
        self.rnd_fh = builtins.open('/dev/urandom', 'rb')
        config('bucket', 'mybucket')
        config('key', 'nbdkit_test')
        config('object-size', str(self.obj_size))
        config('size', str(self.dev_size))

    def tearDown(self) -> None:
        super().tearDown()
        self.ref_fh.close()
        self.rnd_fh.close()

    def get_data(self, len):
        buf = self.rnd_fh.read(len // 2 + 1)
        return base64.b16encode(buf)[:len]

    def compare_to_ref(self):
        fh = self.ref_fh
        bl = self.obj_size
        buf = bytearray(bl)
        fh.seek(0)
        for off in range(0, self.dev_size, self.obj_size):
            ref = fh.read(bl)
            pread(self.s3, buf, off, flags=0)
            self.assertEqual(ref, buf)

    def test_read_hole(self):
        delete_object(self.s3, f"{key_name}/{5:016x}", force=True)
        buf = bytearray(self.obj_size)
        pread(self.s3, buf, 5*self.obj_size, 0)
        self.assertEqual(buf, bytearray(self.obj_size))

    def test_write_memoryview(self):
        buf = memoryview(bytearray(obj_size))
        pwrite(self.s3, buf, 0, 0)
        pwrite(self.s3, buf[10:], 42, 0)

    def test_read(self):
        fh = self.ref_fh
        bl = self.obj_size

        # Fill disk
        fh.seek(0)
        for off in range(0, self.dev_size, self.obj_size):
            buf = self.get_data(bl)
            pwrite(self.s3, buf, offset=off, flags=0)
            fh.write(buf)
        self.compare_to_ref()

        # Test different kinds of read requests
        corner_cases = (
            1, 2,
            bl-2, bl-1, bl+2,
            2*bl-1, 2*bl, 2*bl+1,
            5*bl-5, 5*bl, 5*bl+5)
        for off in (0,) + corner_cases:
            for len_ in corner_cases:
                buf = bytearray(len_)
                pread(self.s3, buf, off, flags=0)
                fh.seek(off)
                ref = fh.read(len_)
                self.assertEqual(buf, ref)

    def test_write(self):
        fh = self.ref_fh
        bl = self.obj_size

        # Fill disk
        fh.seek(0)
        for off in range(0, self.dev_size, self.obj_size):
            buf = self.get_data(bl)
            pwrite(self.s3, buf, offset=off, flags=0)
            fh.write(buf)
        self.compare_to_ref()

        # Test different kinds of write requests
        corner_cases = (
            1, 2,
            bl-2, bl-1, bl+2,
            2*bl-1, 2*bl, 2*bl+1,
            5*bl-5, 5*bl, 5*bl+5)
        for off in (0,) + corner_cases:
            for len_ in corner_cases:
                buf = self.get_data(len_)
                pwrite(self.s3, buf, off, flags=0)
                fh.seek(off)
                fh.write(buf)
                self.compare_to_ref()


# To run unit tests against a real S3 bucket, set the TEST_BUCKET
# environment variable.
@unittest.skipIf('TEST_BUCKET' not in os.environ,
                 'TEST_BUCKET environment variable not defined.')
class RemoteTest(Test):
    def setUp(self):
        super().setUp()
        self.s3 = open(False)
        config('bucket', os.environ['TEST_BUCKET'])
