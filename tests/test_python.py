#!/usr/bin/env python3
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

"""
This tests the Python plugin thoroughly by issuing client commands
through libnbd and checking we get the expected results.  It uses an
associated plugin (test-python-plugin.sh).
"""

import nbd
import unittest
import pickle
import base64


class Test(unittest.TestCase):
    def setUp(self):
        self.h = nbd.NBD()

    def tearDown(self):
        del self.h

    def connect(self, cfg):
        cfg = base64.b64encode(pickle.dumps(cfg)).decode()
        cmd = ["nbdkit", "-v", "-s", "--exit-with-parent",
               "python", "test-python-plugin.py", "cfg=" + cfg]
        self.h.connect_command(cmd)

    def test_none(self):
        """
        Test we can send an empty pickled test configuration and do
        nothing else.  This is just to ensure the machinery of the
        test works.
        """
        self.connect({})

    def test_size_512(self):
        """Test the size."""
        self.connect({"size": 512})
        assert self.h.get_size() == 512

    def test_size_1m(self):
        """Test the size."""
        self.connect({"size": 1024*1024})
        assert self.h.get_size() == 1024*1024

    # Test each flag call.
    def test_is_rotational_true(self):
        self.connect({"size": 512, "is_rotational": True})
        assert self.h.is_rotational()

    def test_is_rotational_false(self):
        self.connect({"size": 512, "is_rotational": False})
        assert not self.h.is_rotational()

    def test_can_multi_conn_true(self):
        self.connect({"size": 512, "can_multi_conn": True})
        assert self.h.can_multi_conn()

    def test_can_multi_conn_false(self):
        self.connect({"size": 512, "can_multi_conn": False})
        assert not self.h.can_multi_conn()

    def test_read_write(self):
        self.connect({"size": 512, "can_write": True})
        assert not self.h.is_read_only()

    def test_read_only(self):
        self.connect({"size": 512, "can_write": False})
        assert self.h.is_read_only()

    def test_can_flush_true(self):
        self.connect({"size": 512, "can_flush": True})
        assert self.h.can_flush()

    def test_can_flush_false(self):
        self.connect({"size": 512, "can_flush": False})
        assert not self.h.can_flush()

    def test_can_trim_true(self):
        self.connect({"size": 512, "can_trim": True})
        assert self.h.can_trim()

    def test_can_trim_false(self):
        self.connect({"size": 512, "can_trim": False})
        assert not self.h.can_trim()

    # nbdkit can always zero because it emulates it.
    # self.connect({"size": 512, "can_zero": True})
    # assert self.h.can_zero()
    # self.connect({"size": 512, "can_zero": False})
    # assert not self.h.can_zero()

    def test_can_fast_zero_true(self):
        self.connect({"size": 512, "can_fast_zero": True})
        assert self.h.can_fast_zero()

    def test_can_fast_zero_false(self):
        self.connect({"size": 512, "can_fast_zero": False})
        assert not self.h.can_fast_zero()

    def test_can_fua_none(self):
        self.connect({"size": 512, "can_fua": "none"})
        assert not self.h.can_fua()

    def test_can_fua_emulate(self):
        self.connect({"size": 512, "can_fua": "emulate"})
        assert self.h.can_fua()

    def test_can_fua_native(self):
        self.connect({"size": 512, "can_fua": "native"})
        assert self.h.can_fua()

    def test_can_cache_none(self):
        self.connect({"size": 512, "can_cache": "none"})
        assert not self.h.can_cache()

    def test_can_cache_emulate(self):
        self.connect({"size": 512, "can_cache": "emulate"})
        assert self.h.can_cache()

    def test_can_cache_native(self):
        self.connect({"size": 512, "can_cache": "native"})
        assert self.h.can_cache()

    # In theory we could use a test like this, but nbdkit can
    # always synthesize base:allocation block_status responses
    # even if the plugin doesn't support them.
    #
    # def test_can_extents_true(self):
    #     self.h.add_meta_context("base:allocation")
    #     self.connect({"size": 512, "can_extents": True})
    #     assert self.h.can_meta_context("base:allocation")
    #
    # def test_can_extents_false(self):
    #     self.h.add_meta_context("base:allocation")
    #     self.connect({"size": 512, "can_extents": False})
    #     assert not self.h.can_meta_context("base:allocation")

    def test_pread(self):
        """Test pread."""
        self.connect({"size": 512})
        buf = self.h.pread(512, 0)
        assert buf == bytearray(512)

    # Test pwrite + flags.
    def test_pwrite(self):
        self.connect({"size": 512})
        buf = bytearray(512)
        self.h.pwrite(buf, 0)

    def test_pwrite_fua(self):
        self.connect({"size": 512,
                      "can_fua": "native",
                      "pwrite_expect_fua": True})
        buf = bytearray(512)
        self.h.pwrite(buf, 0, nbd.CMD_FLAG_FUA)

    def test_flush(self):
        """Test flush."""
        self.connect({"size": 512, "can_flush": True})
        self.h.flush()

    def test_trim(self):
        """Test trim."""
        self.connect({"size": 512, "can_trim": True})
        self.h.trim(512, 0)

    def test_trim_fua(self):
        """Test trim + flags."""
        self.connect({"size": 512,
                      "can_trim": True,
                      "can_fua": "native",
                      "trim_expect_fua": True})
        self.h.trim(512, 0, nbd.CMD_FLAG_FUA)

    def test_zero(self):
        """Test zero."""
        self.connect({"size": 512, "can_zero": True})
        self.h.zero(512, 0, nbd.CMD_FLAG_NO_HOLE)

    def test_zero_fua(self):
        """Test zero + flags."""
        self.connect({"size": 512,
                      "can_zero": True,
                      "can_fua": "native",
                      "zero_expect_fua": True})
        self.h.zero(512, 0, nbd.CMD_FLAG_NO_HOLE | nbd.CMD_FLAG_FUA)

    def test_zero_may_trim(self):
        """Test zero + may trim."""
        self.connect({"size": 512,
                      "can_zero": True,
                      "zero_expect_may_trim": True})
        self.h.zero(512, 0, 0)  # absence of nbd.CMD_FLAG_NO_HOLE

    def test_zero_fast_zero(self):
        """Test zero + fast zero."""
        self.connect({"size": 512,
                      "can_zero": True,
                      "can_fast_zero": True,
                      "zero_expect_fast_zero": True})
        self.h.zero(512, 0, nbd.CMD_FLAG_NO_HOLE | nbd.CMD_FLAG_FAST_ZERO)

    def test_zero_large(self):
        """Test large zero."""
        self.connect({"size": 0x10_0000_0000,
                      "can_zero": True,
                      "create_disk": False})
        self.h.zero(0xf000_0000, 0, nbd.CMD_FLAG_NO_HOLE)

    def test_cache(self):
        """Test cache."""
        self.connect({"size": 512, "can_cache": "native"})
        self.h.cache(512, 0)

    # We don't have access to the magic constants defined in the
    # nbdkit module, so redefine them here.
    EXTENT_HOLE = 1
    EXTENT_ZERO = 2

    def test_extents_1(self):
        """Test extents."""

        offset = None
        entries = []

        def f(meta_context, o, e, err):
            nonlocal offset, entries
            if meta_context != "base:allocation":
                return
            offset = o
            entries = e

        self.h.add_meta_context("base:allocation")
        self.connect({"size": 512,
                      "can_extents": True,
                      "extents":
                      [(0, 512, self.EXTENT_HOLE | self.EXTENT_ZERO)]})

        self.h.block_status(512, 0, f)
        assert offset == 0
        assert entries == [512, self.EXTENT_HOLE | self.EXTENT_ZERO]

    def test_extents_2(self):
        """Test extents."""

        offset = None
        entries = []

        def f(meta_context, o, e, err):
            nonlocal offset, entries
            if meta_context != "base:allocation":
                return
            offset = o
            entries = e

        self.h.add_meta_context("base:allocation")
        self.connect({"size": 2048,
                      "can_extents": True,
                      "extents":
                      [(0, 512, self.EXTENT_HOLE | self.EXTENT_ZERO),
                       (512, 512, 0),
                       (1024, 1024, self.EXTENT_ZERO)]})

        self.h.block_status(2048, 0, f)
        assert offset == 0
        assert entries == [512, self.EXTENT_HOLE | self.EXTENT_ZERO,
                           512, 0,
                           1024, self.EXTENT_ZERO]

        self.h.block_status(1024, 1024, f)
        assert offset == 1024
        assert entries == [1024, self.EXTENT_ZERO]
