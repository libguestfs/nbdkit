# nbdkit
# Copyright (C) 2018-2020 Red Hat Inc.
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

include $(top_srcdir)/common-rules.mk

# These headers contain only common code shared by the core server,
# plugins and/or filters.  They are not installed.
EXTRA_DIST = \
	ascii-ctype.h \
	ascii-string.h \
	byte-swapping.h \
	exit-with-parent.h \
	isaligned.h \
	ispowerof2.h \
	iszero.h \
	minmax.h \
	nextnonzero.h \
	random.h \
	rounding.h \
	tvdiff.h \
	unix-path-max.h \
	$(NULL)

# Unit tests.

TESTS = \
	test-ascii-ctype \
	test-ascii-string \
	test-byte-swapping \
	test-isaligned \
	test-ispowerof2 \
	test-iszero \
	test-minmax \
	test-nextnonzero \
	test-random \
	test-tvdiff \
	$(NULL)
check_PROGRAMS = $(TESTS)

test_ascii_ctype_SOURCES = test-ascii-ctype.c ascii-ctype.h
test_ascii_ctype_CPPFLAGS = -I$(srcdir)
test_ascii_ctype_CFLAGS = $(WARNINGS_CFLAGS)

test_ascii_string_SOURCES = test-ascii-string.c ascii-string.h
test_ascii_string_CPPFLAGS = -I$(srcdir)
test_ascii_string_CFLAGS = $(WARNINGS_CFLAGS)

test_byte_swapping_SOURCES = test-byte-swapping.c byte-swapping.h
test_byte_swapping_CPPFLAGS = -I$(srcdir)
test_byte_swapping_CFLAGS = $(WARNINGS_CFLAGS)

test_isaligned_SOURCES = test-isaligned.c isaligned.h
test_isaligned_CPPFLAGS = -I$(srcdir)
test_isaligned_CFLAGS = $(WARNINGS_CFLAGS)

test_ispowerof2_SOURCES = test-ispowerof2.c ispowerof2.h
test_ispowerof2_CPPFLAGS = -I$(srcdir)
test_ispowerof2_CFLAGS = $(WARNINGS_CFLAGS)

test_iszero_SOURCES = test-iszero.c iszero.h
test_iszero_CPPFLAGS = -I$(srcdir)
test_iszero_CFLAGS = $(WARNINGS_CFLAGS)

test_minmax_SOURCES = test-minmax.c minmax.h
test_minmax_CPPFLAGS = -I$(srcdir)
test_minmax_CFLAGS = $(WARNINGS_CFLAGS)

test_nextnonzero_SOURCES = test-nextnonzero.c nextnonzero.h
test_nextnonzero_CPPFLAGS = -I$(srcdir)
test_nextnonzero_CFLAGS = $(WARNINGS_CFLAGS)

test_random_SOURCES = test-random.c random.h
test_random_CPPFLAGS = -I$(srcdir)
test_random_CFLAGS = $(WARNINGS_CFLAGS)

test_tvdiff_SOURCES = test-tvdiff.c tvdiff.h
test_tvdiff_CPPFLAGS = -I$(srcdir)
test_tvdiff_CFLAGS = $(WARNINGS_CFLAGS)
