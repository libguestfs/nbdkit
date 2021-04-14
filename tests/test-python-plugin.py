# nbdkit test plugin
# Copyright (C) 2019-2020 Red Hat Inc.
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

"""See tests/test-python.sh."""

import nbdkit
import pickle
import base64

API_VERSION = 2

cfg = {}


def config(k, v):
    global cfg
    if k == "cfg":
        cfg = pickle.loads(base64.b64decode(v.encode()))


def config_complete():
    print("set_error = %r" % nbdkit.set_error)


def open(readonly):
    return {
        'disk': bytearray(cfg.get('size', 0))
    }


def get_size(h):
    return len(h['disk'])


def is_rotational(h):
    return cfg.get('is_rotational', False)


def can_multi_conn(h):
    return cfg.get('can_multi_conn', False)


def can_write(h):
    return cfg.get('can_write', True)


def can_flush(h):
    return cfg.get('can_flush', False)


def can_trim(h):
    return cfg.get('can_trim', False)


def can_zero(h):
    return cfg.get('can_zero', False)


def can_fast_zero(h):
    return cfg.get('can_fast_zero', False)


def can_fua(h):
    fua = cfg.get('can_fua', "none")
    if fua == "none":
        return nbdkit.FUA_NONE
    elif fua == "emulate":
        return nbdkit.FUA_EMULATE
    elif fua == "native":
        return nbdkit.FUA_NATIVE


def can_cache(h):
    cache = cfg.get('can_cache', "none")
    if cache == "none":
        return nbdkit.CACHE_NONE
    elif cache == "emulate":
        return nbdkit.CACHE_EMULATE
    elif cache == "native":
        return nbdkit.CACHE_NATIVE


def can_extents(h):
    return cfg.get('can_extents', False)


def pread(h, buf, offset, flags):
    assert flags == 0
    end = offset + len(buf)
    buf[:] = h['disk'][offset:end]


def pwrite(h, buf, offset, flags):
    expect_fua = cfg.get('pwrite_expect_fua', False)
    actual_fua = bool(flags & nbdkit.FLAG_FUA)
    assert expect_fua == actual_fua
    end = offset + len(buf)
    h['disk'][offset:end] = buf


def flush(h, flags):
    assert flags == 0


def trim(h, count, offset, flags):
    expect_fua = cfg.get('trim_expect_fua', False)
    actual_fua = bool(flags & nbdkit.FLAG_FUA)
    assert expect_fua == actual_fua
    h['disk'][offset:offset+count] = bytearray(count)


def zero(h, count, offset, flags):
    expect_fua = cfg.get('zero_expect_fua', False)
    actual_fua = bool(flags & nbdkit.FLAG_FUA)
    assert expect_fua == actual_fua
    expect_may_trim = cfg.get('zero_expect_may_trim', False)
    actual_may_trim = bool(flags & nbdkit.FLAG_MAY_TRIM)
    assert expect_may_trim == actual_may_trim
    expect_fast_zero = cfg.get('zero_expect_fast_zero', False)
    actual_fast_zero = bool(flags & nbdkit.FLAG_FAST_ZERO)
    assert expect_fast_zero == actual_fast_zero
    h['disk'][offset:offset+count] = bytearray(count)


def cache(h, count, offset, flags):
    assert flags == 0
    # do nothing


def extents(h, count, offset, flags):
    return cfg.get('extents', [])
