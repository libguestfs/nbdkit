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
import errno
import os
import re
import tempfile
import threading
from typing import Any, Dict, Optional, Union, List
import unittest
from contextlib import closing, contextmanager
from io import BytesIO
import builtins
from unittest.mock import patch

import boto3
from botocore.exceptions import (
    ClientError, ReadTimeoutError, ConnectTimeoutError, ConnectionClosedError)
import nbdkit

API_VERSION = 2
BinaryData = Union[bytes, bytearray, memoryview]


class Config:
    """Holds configuration data passed in by the user."""

    def __init__(self) -> None:
        self.access_key = None
        self.secret_key = None
        self.session_token = None
        self.endpoint_url = None
        self.bucket_name = None
        self.key_name = None
        self.dev_size = None
        self.obj_size = None

    def set(self, key: str, value: str) -> None:
        """Set a configuration value."""

        if key == "access-key" or key == "access_key":
            self.access_key = value
        elif key == "secret-key" or key == "secret_key":
            self.secret_key = value
        elif key == "session-token" or key == "session_token":
            self.session_token = value
        elif key == "endpoint-url" or key == "endpoint_url":
            self.endpoint_url = value
        elif key == "bucket":
            self.bucket_name = value
        elif key == "key":
            self.key_name = value
        elif key == 'size':
            self.dev_size = nbdkit.parse_size(value)
        elif key == 'object-size':
            self.obj_size = nbdkit.parse_size(value)
        else:
            raise RuntimeError("unknown parameter %s" % key)

    def validate(self) -> None:
        """Validate configuration settings."""

        if self.bucket_name is None:
            raise RuntimeError("bucket parameter missing")
        if self.key_name is None:
            raise RuntimeError("key parameter missing")

        if (self.dev_size and not self.obj_size or
                self.obj_size and not self.dev_size):
            raise RuntimeError(
                "`size` and `object-size` parameters must always be "
                "specified together")

        if self.dev_size and self.dev_size % self.obj_size != 0:
            raise RuntimeError('`size` must be a multiple of `object-size`')


def thread_model():
    return nbdkit.THREAD_MODEL_PARALLEL


def can_write(server):
    return cfg.obj_size is not None


def can_multi_conn(server):
    return True


def can_trim(server):
    return True


def can_zero(server):
    return True


def can_fast_zero(server):
    return True


def can_cache(server):
    return nbdkit.CACHE_NONE


def block_size(server):

    if not cfg.obj_size:
        # More or less arbitrary.
        return (1, 512*1024, 0xffffffff)

    # Should return (minimum, preferred, maximum) block size. We return the
    # same value for all three because even though the plugin can handle
    # arbitrary large and small blocks, the performance penalty is huge and it
    # is always preferable for the client to split up requests as needed.
    return (cfg.obj_size, cfg.obj_size, cfg.obj_size)


def is_rotational(server):
    return False


def can_fua(server):
    return nbdkit.FUA_NATIVE


def can_extents(server):
    return False


def can_flush(server):
    return True


def config(key, value):
    cfg.set(key, value)


def config_complete():
    cfg.validate()


def open(readonly):
    return Server()


def get_size(server):
    return server.get_size()


def pread(server, buf, offset, flags):
    try:
        return server.pread(buf, offset, flags)
    except (ReadTimeoutError, ConnectTimeoutError, ConnectionClosedError):
        nbdkit.debug('S3 connection timed out on pread()')
        nbdkit.set_error(errno.ETIMEDOUT)


def pwrite(server, buf, offset, flags):
    try:
        return server.pwrite(buf, offset, flags)
    except (ReadTimeoutError, ConnectTimeoutError, ConnectionClosedError):
        nbdkit.debug('S3 connection timed out on write()')
        nbdkit.set_error(errno.ETIMEDOUT)


def trim(server, size, offset, flags):
    try:
        return server.trim(size, offset, flags)
    except (ReadTimeoutError, ConnectTimeoutError, ConnectionClosedError):
        nbdkit.debug('S3 connection timed out on trim()')
        nbdkit.set_error(errno.ETIMEDOUT)


def zero(server, size, offset, flags):
    try:
        return server.zero(size, offset, flags)
    except (ReadTimeoutError, ConnectTimeoutError, ConnectionClosedError):
        nbdkit.debug('S3 connection timed out on trim()')
        nbdkit.set_error(errno.ETIMEDOUT)


def flush(server, flags):
    # Flush is implicitly done on every request.
    return


class Server:
    """Handles NBD requests for one connection."""

    def __init__(self) -> None:
        self.s3 = boto3.client(
            "s3", aws_access_key_id=cfg.access_key,
            aws_secret_access_key=cfg.secret_key,
            aws_session_token=cfg.session_token,
            endpoint_url=cfg.endpoint_url)

    def get_size(self) -> int:
        if cfg.dev_size:
            return cfg.dev_size

        try:
            resp = self.s3.head_object(
                Bucket=cfg.bucket_name, Key=cfg.key_name)
        except AttributeError:
            resp = self.s3.get_object(
                Bucket=cfg.bucket_name, Key=cfg.key_name)

        size = resp['ResponseMetadata']['HTTPHeaders']['content-length']
        return int(size)

    def pread(self, buf: Union[bytearray, memoryview],
              offset: int, flags: int) -> None:
        to_read = len(buf)
        if not cfg.obj_size:
            buf[:] = self._get_object(cfg.key_name, size=to_read, off=offset)
            return

        read = 0

        (blockno, block_offset) = divmod(offset, cfg.obj_size)
        while to_read > 0:
            key = f"{cfg.key_name}/{blockno:016x}"
            len_ = min(to_read, cfg.obj_size - block_offset)
            buf[read:read + len_] = self._get_object(
                key, size=len_, off=block_offset
            )
            to_read -= len_
            read += len_
            blockno += 1
            block_offset = 0

    def _get_object(self, obj_name: str, size: int, off: int) -> bytes:
        """Read *size* bytes from *obj_name*, starting at *off*."""

        try:
            resp = self.s3.get_object(Bucket=cfg.bucket_name, Key=obj_name,
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

    def pwrite(self, buf: BinaryData, offset: int, flags: int) -> None:
        # We can ignore FUA flags, because every write is always flushed

        if not cfg.obj_size:
            raise RuntimeError('Unable to write in single-object mode')

        # memoryviews can be sliced without copying the data
        if not isinstance(buf, memoryview):
            buf = memoryview(buf)

        # Calculate block number and offset within the block for the
        # first byte that we need to write.
        (blockno1, block_offset1) = divmod(offset, cfg.obj_size)

        # Calculate block number of the last block that we have to
        # write to, and how many bytes we need to write into it.
        (blockno2, block_len2) = divmod(offset + len(buf), cfg.obj_size)

        # Special case: start and end is within the same one block
        if blockno1 == blockno2 and (block_offset1 != 0 or block_len2 != 0):
            nbdkit.debug(f"pwrite(): write at offset {offset} not aligned, "
                         f"covers bytes {block_offset1} to {block_len2} of "
                         f"block {blockno1}. Rewriting full block...")

            # We could separately fetch the prefix and suffix, but give that
            # we're always writing full blocks, it's likely that the latency of
            # two separate read requests would be much bigger than the savings
            # in volume.
            key = f'{cfg.key_name}/{blockno1:016x}'
            with obj_lock(key):
                fbuf = bytearray(self._get_object(
                    key, size=cfg.obj_size, off=0))
                fbuf[block_offset1:block_len2] = buf
                self._put_object(key, fbuf)
                return

        # First write is not aligned to first block
        if block_offset1:
            nbdkit.debug(
                f"pwrite(): write at offset {offset} not aligned, "
                f"starts {block_offset1} bytes into block {blockno1}. "
                "Rewriting full block...")
            key = f'{cfg.key_name}/{blockno1:016x}'
            with obj_lock(key):
                pre = self._get_object(key, size=block_offset1, off=0)
                len_ = cfg.obj_size - block_offset1
                self._put_object(key, pre + buf[:len_])
                buf = buf[len_:]
                blockno1 += 1

        # Last write is not a full block
        if block_len2:
            nbdkit.debug(f"pwrite(): write at offset {offset} not aligned, "
                         f"ends {cfg.obj_size-block_len2} bytes before block "
                         f"{blockno2+1}. Rewriting full block...")
            key = f'{cfg.key_name}/{blockno2:016x}'
            with obj_lock(key):
                len_ = cfg.obj_size - block_len2
                post = self._get_object(key, size=len_, off=block_len2)
                self._put_object(key, concat(buf[-block_len2:], post))
                buf = buf[:-block_len2]

        off = 0
        for blockno in range(blockno1, blockno2):
            key = f"{cfg.key_name}/{blockno:016x}"
            with obj_lock(key):
                nbdkit.debug(f"pwrite(): writing block {blockno}...")
                self._put_object(key, buf[off:off + cfg.obj_size])
                off += cfg.obj_size

    def _put_object(self, obj_name: str, buf: BinaryData) -> None:
        "Write *buf* into *obj_name"

        assert len(buf) == cfg.obj_size

        # Boto does not support reading from memoryviews :-(
        if isinstance(buf, memoryview):
            buf = buf.tobytes()
        self.s3.put_object(Bucket=cfg.bucket_name, Key=obj_name, Body=buf)

    def zero(self, size: int, offset: int, flags: int) -> None:
        nbdkit.debug(f'Processing zero(size={size}, off={offset})')
        if size == 0:
            return

        if flags & nbdkit.FLAG_MAY_TRIM:
            return self.trim(size, offset, 0)

        # Calculate block number and offset within the block for the
        # first byte that we need to write.
        (blockno1, block_offset1) = divmod(offset, cfg.obj_size)

        # Calculate block number of the last block that we have to
        # write to, and how many bytes we need to write into it.
        (blockno2, block_len2) = divmod(offset + size, cfg.obj_size)

        if blockno1 == blockno2:
            nbdkit.debug(f'Zeroing {size} bytes in block {blockno1} '
                         f'(offset {offset})')
            self.pwrite(bytearray(size), offset=offset, flags=0)
            return

        if block_offset1:
            len_ = cfg.obj_size - block_offset1
            nbdkit.debug(f'Zeroing last {len_} bytes of block {blockno1} '
                         f'(offset {offset})')
            self.pwrite(bytearray(len_), offset=offset, flags=0)
            blockno1 += 1

        if block_len2:
            off = cfg.obj_size * blockno2
            nbdkit.debug(f'Zeroing first {block_len2-1} bytes of block '
                         f'{blockno2} (offset {off})')
            self.pwrite(bytearray(block_len2), offset=off, flags=0)

        self._delete_objects(blockno1, blockno2)

    def trim(self, size: int, offset: int, flags: int) -> None:
        nbdkit.debug(f'Processing trim(size={size}, off={offset})')
        if size == 0:
            return

        # Calculate block number and offset within the block for the
        # first byte that we need to trim.
        (blockno1, block_offset1) = divmod(offset, cfg.obj_size)

        # Calculate block number of the last block that we have to
        # trim fully.
        (blockno2, _) = divmod(offset + size, cfg.obj_size)

        if block_offset1 != 0:
            blockno1 += 1

        if blockno1 == blockno2:
            nbdkit.debug('nothing to delete')
            return

        self._delete_objects(blockno1, blockno2)

    def _delete_objects(self, first: int, last: int) -> None:
        """Delete objects *first* (inclusive) to *last* (exclusive)"""
        nbdkit.debug(f'Deleting objects {first} to {last}...')

        if first >= last:
            return

        if first == 0:
            start_after = ''
        else:
            start_after = f"{cfg.key_name}/{first-1:016x}"
        last_key = f"{cfg.key_name}/{last:016x}"

        to_delete = []
        for key in self._list_objects(f"{cfg.key_name}/",
                                      start_after=start_after):
            if key >= last_key:
                break
            to_delete.append({'Key': key})
            nbdkit.debug(f'Marking object {key} for removal')
            if len(to_delete) >= 1000:
                resp = self.s3.delete_objects(Bucket=cfg.bucket_name, Delete={
                    'Objects': to_delete, 'Quiet': True})
                if resp.get('Errors', None):
                    raise RuntimeError(
                        'Failed to delete objects: %s' % resp['Errors'])
                del to_delete[:]

        if not to_delete:
            return

        resp = self.s3.delete_objects(Bucket=cfg.bucket_name, Delete={
            'Objects': to_delete, 'Quiet': True})
        if resp.get('Errors', None):
            raise RuntimeError(
                'Failed to delete objects: %s' % resp['Errors'])

    def _list_objects(self, prefix: str,
                      start_after: Optional[str] = None) -> List[str]:
        """Return keys for objects in  bucket.

        Lists all keys starting with *prefix* in lexicographic order, starting
        with the key following *start_after*.
        """

        args = {
            'Bucket': cfg.bucket_name,
            'Prefix': prefix,
        }

        if start_after is not None:
            args['StartAfter'] = start_after

        while True:
            resp = self.s3.list_objects_v2(**args)
            if not 'Contents' in resp:
                return
            for el in resp['Contents']:
                yield el['Key']
            if not resp['IsTruncated']:
                return
            args['ContinuationToken'] = resp['NextContinuationToken']


def concat(b1, b2):
    """Concatenate two byte-like objects (including memoryviews)"""

    l1 = len(b1)
    l3 = l1 + len(b2)
    b3 = bytearray(l3)
    b3[:l1] = b1
    b3[l1:] = b2
    return b3


def put_object(s3, obj_name, buf):
    "Write *buf* into *obj_name"

    assert len(buf) == cfg.obj_size

    # Boto does not support reading from memoryviews :-(
    if isinstance(buf, memoryview):
        buf = buf.tobytes()
    s3.put_object(Bucket=cfg.bucket_name, Key=obj_name, Body=buf)


def make_client_error(errcode):
    return ClientError(
        error_response={'Error': {'Code': errcode}},
        operation_name='unspecified')


class MultiLock:
    """Provides locking for large amounts of entities.

    This class provides locking for a dynamically changing  and potentially
    large set of entities, avoiding the need to allocate a separate lock for
    each entity. The `acquire` and `release` methods have an additional
    argument, the locking key, and only locks with the same key can see each
    other (ie, several threads can hold locks with different locking keys at
    the same time).
    """

    def __init__(self):
        self.locked_keys = set()
        self.cond = threading.Condition()

    @contextmanager
    def __call__(self, key):
        self.acquire(key)
        try:
            yield
        finally:
            self.release(key)

    def acquire(self, key):
        '''Acquire lock for given key.'''

        with self.cond:
            while key in self.locked_keys:
                self.cond.wait()

            self.locked_keys.add(key)

    def release(self, key):
        """Release lock on given key"""

        with self.cond:
            self.locked_keys.remove(key)
            self.cond.notify_all()


###################
# Global state    #
###################
cfg = Config()
obj_lock = MultiLock()


######################
# Unit Tests         #
######################

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

    def delete_objects(self, Bucket: str, Delete: Dict[str, Any]):
        for el in Delete['Objects']:
            key = (Bucket, el['Key'])
            if key not in self.keys:
                raise make_client_error('NoSuchKey')
            del self.keys[key]

        return {'Errors': []}

    def list_objects_v2(self, Bucket: str, ContinuationToken: str = '',
                        Prefix: str = '', StartAfter: str = ''):
        assert not ContinuationToken
        all_keys = sorted(x[1] for x in self.keys
                          if x[0] == Bucket and x[1].startswith(Prefix))
        contents = []
        for k in all_keys:
            if k <= StartAfter:
                continue
            contents.append({'Key': k})
        return {'IsTruncated': False,
                'Contents': contents,
                'NextContinuationToken': ''}


class LocalTest(unittest.TestCase):
    def setUp(self):
        super().setUp()

        self.obj_size = 16
        self.dev_size = 20*self.obj_size
        self.ref_fh = tempfile.TemporaryFile()
        self.ref_fh.truncate(cfg.dev_size)
        self.rnd_fh = builtins.open('/dev/urandom', 'rb')

        config('bucket', 'testbucket')
        config('key', 'nbdkit_test')
        config('object-size', str(self.obj_size))
        config('size', str(self.dev_size))

        config_complete()
        with patch.object(boto3, 'client') as mock_client:
            mock_client.return_value = MockS3Client()
            self.s3 = open(False)

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
            self.assertEqual(
                ref, buf, f'mismatch at off={off} (blk {off//self.obj_size})')

    def test_write_memoryview(self):
        buf = memoryview(bytearray(cfg.obj_size))
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

    def test_zero(self):
        fh = self.ref_fh
        bl = self.obj_size

        # Fill disk
        fh.seek(0)
        for off in range(0, self.dev_size, self.obj_size):
            buf = self.get_data(bl)
            pwrite(self.s3, buf, offset=off, flags=0)
            fh.write(buf)
        self.compare_to_ref()

        # Test different kinds of zero requests
        corner_cases = (
            1, 2,
            bl-2, bl-1, bl+2,
            2*bl-1, 2*bl, 2*bl+1,
            5*bl-5, 5*bl, 5*bl+5)
        for off in (0,) + corner_cases:
            for len_ in corner_cases:
                zero(self.s3, len_, off, flags=0)
                fh.seek(off)
                fh.write(bytearray(len_))
                self.compare_to_ref()

                # Re-fill with data
                buf = self.get_data(len_)
                pwrite(self.s3, buf, off, flags=0)
                fh.seek(off)
                fh.write(buf)

    def test_trim(self):
        bl = self.obj_size

        # Fill disk
        for off in range(0, self.dev_size, self.obj_size):
            pwrite(self.s3, self.get_data(bl), offset=off, flags=0)

        # Test different kinds of trim requests
        corner_cases = (
            1, 2,
            bl-2, bl-1, bl+2,
            2*bl-1, 2*bl, 2*bl+1,
            5*bl-5, 5*bl, 5*bl+5)
        for off in (0,) + corner_cases:
            for len_ in corner_cases:
                (b1, o1) = divmod(off, bl)
                (b2, o2) = divmod(off + len_, bl)

                obj_count1 = len(list(self.s3._list_objects(cfg.key_name)))
                trim(self.s3, len_, off, flags=0)
                obj_count2 = len(list(self.s3._list_objects(cfg.key_name)))

                blocks_to_delete = b2-b1
                if o1 != 0 and blocks_to_delete >= 1:
                    blocks_to_delete -= 1

                self.assertEqual(obj_count1 - blocks_to_delete, obj_count2)

                # Re-fill with data
                pwrite(self.s3, self.get_data(len_), off, flags=0)


# To run unit tests against a real S3 bucket, set the TEST_BUCKET and
# (optionally) TEST_ENDPOINT environment variables. The point of these tests is
# to make sure we're calling boto correctly, not to test any plugin code.
@unittest.skipIf('TEST_BUCKET' not in os.environ,
                 'TEST_BUCKET environment variable not defined.')
class RemoteTest(unittest.TestCase):
    def setUp(self):
        super().setUp()

        self.obj_size = 64
        self.dev_size = 20*self.obj_size

        config('bucket', os.environ['TEST_BUCKET'])
        if 'TEST_ENDPOINT' in os.environ:
            config('endpoint-url', os.environ['TEST_ENDPOINT'])
        config('key', 'nbdkit_test')
        config('object-size', str(self.obj_size))
        config('size', str(self.dev_size))

        config_complete()
        self.s3 = open(False)
        self.rnd_fh = builtins.open('/dev/urandom', 'rb')

    def tearDown(self) -> None:
        super().tearDown()
        self.rnd_fh.close()

    def get_data(self, len):
        buf = self.rnd_fh.read(len // 2 + 1)
        return base64.b16encode(buf)[:len]

    def test_zero(self):
        bs = self.obj_size
        ref_buf = bytearray(self.get_data(3*bs))
        pwrite(self.s3, ref_buf, 0, 0)

        zero_start = bs//2
        zero(self.s3, 2*bs, zero_start, 0)
        ref_buf[zero_start:zero_start + 2*bs] = bytearray(2*bs)

        buf = bytearray(len(ref_buf))
        pread(self.s3, buf, 0, 0)
        self.assertEqual(buf, ref_buf)

    def test_trim(self):
        ref_buf = bytearray(self.get_data(3*self.obj_size))
        pwrite(self.s3, ref_buf, 0, 0)

        obj_count1 = len(list(self.s3._list_objects(cfg.key_name)))

        trim_start = self.obj_size//2
        trim(self.s3, 2*self.obj_size, trim_start, 0)

        obj_count2 = len(list(self.s3._list_objects(cfg.key_name)))
        self.assertEqual(obj_count1-1, obj_count2)

    def test_readwrite(self):
        ref_buf = self.get_data(2*self.obj_size)
        start_off = self.obj_size//2
        pwrite(self.s3, ref_buf, start_off, 0)
        buf = bytearray(len(ref_buf))
        pread(self.s3, buf, start_off, 0)
        self.assertEqual(buf, ref_buf)
