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

include $(top_srcdir)/common-rules.mk

SUBDIRS = win32

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	$(NULL)

noinst_LTLIBRARIES = libcompat.la
# sources should be empty
libcompat_la_SOURCES =
libcompat_la_LIBADD = $(LTLIBOBJS)

EXTRA_DIST = \
	fdatasync.c \
	fdatasync.h \
	fsync.c \
	fsync.h \
	get_current_dir_name.c \
	get_current_dir_name.h \
	getdelim.c \
	getline.c \
	getline.h \
	openlog.c \
	open_memstream.c \
	open_memstream.h \
	poll.c \
	poll.h \
	posix_memalign.c \
	posix_memalign.h \
	pread.c \
	pread.h \
	pwrite.c \
	pwrite.h \
	realpath.c \
	realpath.h \
	strndup.c \
	strndup.h \
	sysconf.c \
	sysconf.h \
	syslog.c \
	syslog.h \
	vsyslog.c

TESTS = \
	test-current-dir-name
check_PROGRAMS = $(TESTS)

test_current_dir_name_SOURCES = \
	test-current-dir-name.c \
	get_current_dir_name.c \
	get_current_dir_name.h \
	$(NULL)
test_current_dir_name_CPPFLAGS = -I$(srcdir)
test_current_dir_name_CFLAGS = $(WARNINGS_CFLAGS)
