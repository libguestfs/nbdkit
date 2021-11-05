# nbdkit
# Copyright (C) 2019 Red Hat Inc.
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

EXTRA_DIST = windows-errors.txt

noinst_LTLIBRARIES = libutils.la

libutils_la_SOURCES = \
	cleanup.c \
	cleanup-nbdkit.c \
	cleanup.h \
	environ.c \
	full-rw.c \
	quote.c \
	utils.c \
	utils.h \
	vector.c \
	vector.h \
	windows-compat.h \
	windows-compat.c \
	windows-errors.c \
	$(NULL)
libutils_la_CPPFLAGS = \
	-I$(top_srcdir)/include \
	$(NULL)
libutils_la_CFLAGS = \
	$(WARNINGS_CFLAGS) \
	$(PTHREAD_CFLAGS) \
	$(NULL)
libutils_la_LIBADD = \
	$(PTHREAD_LIBS) \
	$(NULL)

# Generate the code to map Winsock errors to errno codes.
BUILT_SOURCES = windows-errors.c
windows-errors.c: windows-errors.txt
	@rm -f $@ $@-t
	@echo '/* Generated from windows-errors.txt */' > $@-t
	@echo '#include <nbdkit-plugin.h>' >> $@-t
	@echo '#ifdef WIN32' >> $@-t
	@echo '#include <winsock2.h>' >> $@-t
	@echo '#include <ws2tcpip.h>' >> $@-t
	@echo '#include <windows.h>' >> $@-t
	@echo '#include <errno.h>' >> $@-t
	@echo 'int' >> $@-t
	@echo 'translate_winsock_error (const char *fn, int err) {' >> $@-t
# Always log the original error.
	@echo '    nbdkit_debug ("%s: winsock error %d", fn, err);' >> $@-t
	@echo '    switch (err) {' >> $@-t
	@$(SED) -e '/^#/d' \
	       -e '/^$$/d' \
	       -e 's/\(.*\)[[:space:]][[:space:]]*\(.*\)/#if defined(\1) \&\& defined(\2)\
    case \1: return \2;\
\#endif/' \
	< $< >> $@-t
	@echo '    default:' >> $@-t
	@echo '    return err > 10000 && err < 10025 ? err - 10000 : EINVAL;' >> $@-t
	@echo '    }' >> $@-t
	@echo '}' >> $@-t
	@echo '#endif /* WIN32 */' >> $@-t
	mv $@-t $@
	chmod -w $@

# Unit tests.

TESTS = test-quotes test-vector
check_PROGRAMS = test-quotes test-vector

test_quotes_SOURCES = test-quotes.c quote.c utils.h
test_quotes_CPPFLAGS = -I$(srcdir)
test_quotes_CFLAGS = $(WARNINGS_CFLAGS)

test_vector_SOURCES = test-vector.c vector.c vector.h bench.h
test_vector_CPPFLAGS = -I$(srcdir)
test_vector_CFLAGS = $(WARNINGS_CFLAGS)

bench: test-vector
	NBDKIT_BENCH=1 ./test-vector
