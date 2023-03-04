# -*- python -*-
# nbdkit
# Copyright Red Hat
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

# This fake boto3 module is used to test the S3 plugin.  See also
# tests/test-S3.sh

import re


buf = b'x'*4096 + b'y'*2048 + b'z'*2048


class client(object):
    def __init__(
            self,
            type=None,
            aws_access_key_id=None,
            aws_secret_access_key=None,
            aws_session_token=None,
            endpoint_url=None):
        assert type == "s3"
        assert aws_access_key_id == "TEST_ACCESS_KEY"
        assert aws_secret_access_key == "TEST_SECRET_KEY"
        assert aws_session_token == "TEST_SESSION_TOKEN"
        assert endpoint_url == "TEST_ENDPOINT"

    def get_object(
            self,
            Bucket=None,
            Key=None,
            Range=None):
        assert Bucket == "MY_FILES"
        assert Key == "MY_KEY"

        if Range is None:
            # Return the size in a 'ResponseMetadata'.
            return {'ResponseMetadata':
                    {'HTTPHeaders':
                     {'content-length': len(buf)}}}
        else:
            # Range must be something like "bytes=N-M".
            r = re.match("bytes=(\\d+)-(\\d+)", Range)
            assert r
            start = int(r.group(1))
            end = int(r.group(2))
            b = buf[start:end+1]

            # Return the data in a 'StreamingBody'.
            return {'Body': StreamingBody(b)}


class StreamingBody(object):
    def __init__(self, data):
        self._data = data

    def read(self, size=None):
        if size:
            return self._data[:size]
        else:
            return self._data

    def close(self):
        pass
