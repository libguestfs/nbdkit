#!@sbindir@/nbdkit python
# -*- python -*-
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

# Note this uses API v1 since we may wish to copy it and use it with
# older versions of nbdkit.  Also it doesn't use get_ready() for the
# same reason.

import builtins
import os.path
import tarfile
import nbdkit

tar = None                      # Tar file.
f = None                        # File within the tar file.
offset = None                   # Offset of file within the tar file.
size = None                     # Size of file within the tar file.

def config(k, v):
    global tar, f

    if k == "tar":
        tar = os.path.abspath(v)
    elif k == "file":
        f = v
    else:
        raise RuntimeError("unknown parameter: %s" % key)

# Check all the config parameters were set.
def config_complete():
    global tar, f, offset, size

    if tar is None or f is None:
        raise RuntimeError("tar or file parameter was not set")
    if not os.path.exists(tar):
        raise RuntimeError("$s: file not found" % tar)

    # Find the extent of the file within the tar file.
    for m in tarfile.open(tar, mode='r:'):
        if m.name == f:
            offset = m.offset_data
            size = m.size
    if offset is None or size is None:
        raise RuntimeError("offset or size could not be parsed.  Probably the tar file is not a tar file or the file does not exist in the tar file.")

# Accept a connection from a client, create and return the handle
# which is passed back to other calls.
def open(readonly):
    global tar
    if readonly:
        mode = 'rb'
    else:
        mode = 'r+b'
    return { 'fh': builtins.open(tar, mode) }

# Close the connection.
def close(h):
    h['fh'].close()

# Return the size.
def get_size(h):
    global size
    return size

# Read.
#
# Python plugin thread model is always
# NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS so seeking here is fine.
def pread(h, count, offs):
    global offset
    h['fh'].seek(offset + offs)
    return h['fh'].read(count)

# Write.
def pwrite(h, buf, offs):
    global offset
    h['fh'].seek(offset + offs)
    h['fh'].write(buf)
