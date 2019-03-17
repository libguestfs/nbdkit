# nbdkit
# Copyright (C) 2013-2018 Red Hat Inc.
# All rights reserved.
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
	background.c \
	captive.c \
	cleanup.c \
	connections.c \
	crypto.c \
	debug.c \
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
	protocol.h \
	protostrings.c \
	quit.c \
	signals.c \
	socket-activation.c \
	sockets.c \
	threadlocal.c \
	usergroup.c \
	utils.c \
	$(top_srcdir)/include/nbdkit-plugin.h \
	$(top_srcdir)/include/nbdkit-filter.h

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
	-I$(top_srcdir)/common/utils
nbdkit_CFLAGS = \
	$(PTHREAD_CFLAGS) \
	$(WARNINGS_CFLAGS) \
	$(GNUTLS_CFLAGS) \
	$(LIBSELINUX_CFLAGS) \
	$(VALGRIND_CFLAGS)
nbdkit_LDADD = \
	$(GNUTLS_LIBS) \
	$(LIBSELINUX_LIBS) \
	$(DL_LIBS) \
	$(top_builddir)/common/utils/libutils.la
nbdkit_LDFLAGS = \
	$(PTHREAD_LIBS) \
	$(DL_LDFLAGS)
if USE_LINKER_SCRIPT_FOR_SERVER
nbdkit_LDFLAGS += -Wl,--version-script=$(srcdir)/nbdkit.syms
endif

# protostrings.c is generated from the protocol.h header file where it
# is used to map NBD protocol flags to strings.

BUILT_SOURCES = protostrings.c
EXTRA_DIST += protostrings.c protostrings.sed
CLEANFILES += protostrings.c
protostrings.c: protocol.h protostrings.sed Makefile
	rm -f $@ $@-t
	$(SED) -n -f $(srcdir)/protostrings.sed < $< > $@-t
	mv $@-t $@
	chmod 0444 $@

# synopsis.c is generated from docs/synopsis.txt where it is also
# used to generate the man page.  It is included in main.c.

BUILT_SOURCES += synopsis.c
EXTRA_DIST += synopsis.c
nbdkit_DEPENDENCIES = synopsis.c
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

TESTS = test-utils

check_PROGRAMS = test-utils

test_utils_SOURCES = \
	test-utils.c \
	utils.c \
	cleanup.c
test_utils_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/common/include
test_utils_CFLAGS = $(WARNINGS_CFLAGS) $(VALGRIND_CFLAGS)
