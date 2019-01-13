# nbdkit
# Copyright (C) 2013-2020 Red Hat Inc.
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

EXTRA_DIST = nbdkit.syms

sbin_PROGRAMS = nbdkit

nbdkit_SOURCES = \
	backend.c \
	background.c \
	captive.c \
	connections.c \
	crypto.c \
	debug.c \
	debug-flags.c \
	exports.c \
	extents.c \
	filters.c \
	internal.h \
	locks.c \
	log.c \
	log-stderr.c \
	log-syslog.c \
	main.c \
	options.h \
	plugins.c \
	protocol.c \
	protocol-handshake.c \
	protocol-handshake-oldstyle.c \
	protocol-handshake-newstyle.c \
	public.c \
	quit.c \
	signals.c \
	socket-activation.c \
	sockets.c \
	threadlocal.c \
	usergroup.c \
	vfprintf.c \
	$(top_srcdir)/include/nbdkit-plugin.h \
	$(top_srcdir)/include/nbdkit-filter.h \
	$(NULL)
if ENABLE_LIBFUZZER
nbdkit_SOURCES += fuzzer.c
endif

nbdkit_CPPFLAGS = \
	-Dbindir=\"$(bindir)\" \
	-Dlibdir=\"$(libdir)\" \
	-Dmandir=\"$(mandir)\" \
	-Dplugindir=\"$(plugindir)\" \
	-Dfilterdir=\"$(filterdir)\" \
	-Dsbindir=\"$(sbindir)\" \
	-Dsysconfdir=\"$(sysconfdir)\" \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/common/include \
	-I$(top_srcdir)/common/protocol \
	-I$(top_srcdir)/common/replacements \
	-I$(top_srcdir)/common/utils \
	$(NULL)
nbdkit_CFLAGS = \
	$(PTHREAD_CFLAGS) \
	$(WARNINGS_CFLAGS) \
	$(GNUTLS_CFLAGS) \
	$(LIBSELINUX_CFLAGS) \
	$(VALGRIND_CFLAGS) \
	$(NULL)
nbdkit_LDADD = \
	$(GNUTLS_LIBS) \
	$(LIBSELINUX_LIBS) \
	$(DL_LIBS) \
	$(top_builddir)/common/protocol/libprotocol.la \
	$(top_builddir)/common/utils/libutils.la \
	$(top_builddir)/common/replacements/libcompat.la \
	$(NULL)
nbdkit_LDFLAGS = \
	$(PTHREAD_LIBS) \
	$(DL_LDFLAGS) \
	$(NULL)
if USE_LINKER_SCRIPT_FOR_SERVER
# We have to disable the linker script for libFuzzer because Clang
# adds loads of fuzzer and ASAN-related symbols that are required by
# the plugins but which our linker script tries to hide.
if !ENABLE_LIBFUZZER
nbdkit_LDFLAGS += -Wl,--version-script=$(srcdir)/nbdkit.syms
endif
endif

# synopsis.c is generated from docs/synopsis.txt where it is also
# used to generate the man page.  It is included in main.c.

BUILT_SOURCES = synopsis.c
EXTRA_DIST += synopsis.c
EXTRA_nbdkit_DEPENDENCIES = synopsis.c
CLEANFILES += synopsis.c
main.c: synopsis.c
synopsis.c: $(top_srcdir)/docs/synopsis.txt
	rm -f $@ $@-t
	$(SED) -e 's/\(.*\)/"\1\\n"/g' $< > $@-t
	mv $@-t $@

# pkg-config / pkgconf

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = nbdkit.pc

# Unit testing

TESTS = test-public

check_PROGRAMS = test-public

test_public_SOURCES = \
	test-public.c \
	public.c \
	exports.c \
	extents.c \
	$(NULL)
test_public_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/common/include \
	-I$(top_srcdir)/common/protocol \
	-I$(top_srcdir)/common/replacements \
	-I$(top_srcdir)/common/utils \
	$(NULL)
test_public_CFLAGS = \
	$(PTHREAD_CFLAGS) \
	$(WARNINGS_CFLAGS) \
	$(VALGRIND_CFLAGS) \
	$(NULL)
test_public_LDFLAGS = \
	$(PTHREAD_LIBS) \
	$(NULL)
test_public_LDADD = \
	$(top_builddir)/common/utils/libutils.la \
	$(NULL)
