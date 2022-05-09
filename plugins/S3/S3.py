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
from contextlib import closing


API_VERSION = 2

access_key = None
secret_key = None
session_token = None
endpoint_url = None
bucket_name = None
key_name = None


def thread_model():
    return nbdkit.THREAD_MODEL_PARALLEL


def config(key, value):
    global access_key, secret_key, session_token, endpoint_url, \
           bucket_name, key_name

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
    else:
        raise RuntimeError("unknown parameter %s" % key)


def config_complete():
    if bucket_name is None:
        raise RuntimeError("bucket parameter missing")
    if key_name is None:
        raise RuntimeError("key parameter missing")


def open(readonly):
    s3 = boto3.client("s3",
                      aws_access_key_id=access_key,
                      aws_secret_access_key=secret_key,
                      aws_session_token=session_token,
                      endpoint_url=endpoint_url)
    return s3


def get_size(s3):
    resp = s3.head_object(Bucket=bucket_name, Key=key_name)
    size = resp['ResponseMetadata']['HTTPHeaders']['content-length']
    return int(size)


def pread(s3, buf, offset, flags):
    size = len(buf)
    rnge = 'bytes=%d-%d' % (offset, offset+size-1)
    resp = s3.get_object(Bucket=bucket_name, Key=key_name, Range=rnge)
    body = resp['Body']
    with closing(body):
        buf[:] = body.read(size)
