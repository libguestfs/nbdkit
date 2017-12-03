# nbdkit
# Copyright (C) 2013-2017 Red Hat Inc.
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

CLEANFILES = *~ *.cmi *.cmx *.cmxa *.so
MAINTAINERCLEANFILES =

EXTRA_DIST = \
	functions.sh \
	make-pki.sh \
	README.tests \
	shebang.pl \
	test-captive.sh \
	test-cxx.sh \
	test-dump-config.sh \
	test-dump-plugin.sh \
	test-foreground.sh \
	test-help.sh \
	test-help-plugin.sh \
	test-ipv4.sh \
	test_ocaml_plugin.ml \
	test-ocaml.c \
	test-parallel-file.sh \
	test-parallel-nbd.sh \
	test.pl \
	test.py \
	test.rb \
	test-shebang.sh \
	test-single.sh \
	test-start.sh \
	test-random-sock.sh \
	test-tls.sh \
	test-version.sh \
	test-version-plugin.sh

# Use 'make check' to run the ordinary tests.  To run all the tests
# under valgrind, use the following rule:
check-valgrind:
	NBDKIT_VALGRIND=1 $(MAKE) check

# Basic server command line and start-up tests.

check_PROGRAMS =
check_DATA =
check_SCRIPTS =
check_LTLIBRARIES =
noinst_LTLIBRARIES =

# Ensure we're testing the local copy by running everything through
# the nbdkit helper script in the top build directory.
TESTS_ENVIRONMENT = PATH=$(abs_top_builddir):$(PATH)

TESTS = \
	test-help.sh \
	test-version.sh \
	test-dump-config.sh

if HAVE_PLUGINS

TESTS += \
	test-help-plugin.sh \
	test-version-plugin.sh \
	test-dump-plugin.sh \
	test-start.sh \
	test-single.sh \
	test-captive.sh \
	test-random-sock.sh \
	test-tls.sh \
	test-ipv4.sh \
	test-socket-activation \
	test-foreground.sh

check_PROGRAMS += \
	test-socket-activation

test_socket_activation_SOURCES = test-socket-activation.c
test_socket_activation_CFLAGS = $(WARNINGS_CFLAGS)

endif HAVE_PLUGINS

if HAVE_CXX
# This builds a plugin using the C++ compiler.  The plugin
# doesn't do anything interesting when run.
TESTS += \
	test-cxx.sh
# check_LTLIBRARIES won't build a shared library (see automake manual).
# So we have to do this and add a dependency.
noinst_LTLIBRARIES += \
	test-cxx-plugin.la
test-cxx.sh: test-cxx-plugin.la

test_cxx_plugin_la_SOURCES = \
	test-cxx-plugin.cpp \
	$(top_srcdir)/include/nbdkit-plugin.h
test_cxx_plugin_la_CPPFLAGS = \
	-I$(top_srcdir)/include
test_cxx_plugin_la_CXXFLAGS = \
        $(WARNINGS_CFLAGS)
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
test_cxx_plugin_la_LDFLAGS = \
	-module -avoid-version -shared -rpath /nowhere
endif HAVE_CXX

# Exit with parent test.
check_PROGRAMS += test-exit-with-parent
TESTS += test-exit-with-parent

test_exit_with_parent_SOURCES = test-exit-with-parent.c test.h
test_exit_with_parent_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)

# PKI files for the TLS tests.
check_DATA += pki/.stamp
pki/.stamp: $(srcdir)/make-pki.sh
	$(srcdir)/make-pki.sh

if HAVE_PLUGINS
# Common data shared by multiple tests
check_DATA += file-data
MAINTAINERCLEANFILES += file-data
file-data:
	rm -f $@ $@-t
	for f in `seq 1 512`; do echo -ne '\x01\x02\x03\x04\x05\x06\x07\x08'; done > $@-t
	mv $@-t $@

# While most tests need libguestfs, testing parallel I/O is easier when
# using qemu-io to kick off asynchronous requests.
if HAVE_QEMU_IO
TESTS_ENVIRONMENT += QEMU_IO=$(QEMU_IO)
TESTS += test-parallel-file.sh
TESTS += test-parallel-nbd.sh
endif HAVE_QEMU_IO

endif HAVE_PLUGINS


# Most in-depth tests need libguestfs, since that is a convenient way to
# drive qemu.

if HAVE_LIBGUESTFS

# Use the 'direct' backend, and ensure maximum libguestfs debugging is
# written to the *.log files in case there is a problem.
TESTS_ENVIRONMENT += \
	LIBGUESTFS_ATTACH_METHOD=appliance \
	LIBGUESTFS_DEBUG=1 \
	LIBGUESTFS_TRACE=1 \
	LD_LIBRARY_PATH=../plugins/ocaml/.libs:$(LD_LIBRARY_PATH)

# Common test library.
check_LTLIBRARIES += libtest.la
libtest_la_SOURCES = test.c test.h
libtest_la_CFLAGS = $(WARNINGS_CFLAGS)

if HAVE_PLUGINS

# Basic connection test.
check_PROGRAMS += test-connect
TESTS += test-connect

test_connect_SOURCES = test-connect.c test.h
test_connect_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_connect_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# newstyle protocol test.
check_PROGRAMS += test-newstyle
TESTS += test-newstyle

test_newstyle_SOURCES = test-newstyle.c test.h
test_newstyle_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_newstyle_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# oldstyle protocol test.
check_PROGRAMS += test-oldstyle
TESTS += test-oldstyle

test_oldstyle_SOURCES = test-oldstyle.c test.h
test_oldstyle_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_oldstyle_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# file plugin test.
check_PROGRAMS += test-file
TESTS += test-file

test_file_SOURCES = test-file.c test.h
test_file_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_file_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# gzip plugin test.
if HAVE_ZLIB
if HAVE_GUESTFISH

check_PROGRAMS += test-gzip
TESTS += test-gzip
check_DATA += disk disk.gz
MAINTAINERCLEANFILES += disk disk.gz

test_gzip_SOURCES = test-gzip.c test.h
test_gzip_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_gzip_LDADD = libtest.la $(LIBGUESTFS_LIBS)

disk:
	rm -f $@ test1.img
	guestfish -N fs -m /dev/sda1 write /hello.txt "hello,world"
	mv test1.img disk

disk.gz: disk
	rm -f $@
	gzip -9 -c disk > $@

endif HAVE_GUESTFISH
endif HAVE_ZLIB

# memory plugin test.
check_PROGRAMS += test-memory
TESTS += test-memory

test_memory_SOURCES = test-memory.c test.h
test_memory_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_memory_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# nbd plugin test.
check_PROGRAMS += test-nbd
TESTS += test-nbd

test_nbd_SOURCES = test-nbd.c test.h
test_nbd_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_nbd_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# null plugin test.
check_PROGRAMS += test-null
TESTS += test-null

test_null_SOURCES = test-null.c test.h
test_null_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_null_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# OCaml plugin test.
if HAVE_OCAML

check_PROGRAMS += test-ocaml
TESTS += test-ocaml

# This is somewhat different from the other tests because we have
# to build an actual plugin here.
test_ocaml_SOURCES = test-ocaml.c test.h
test_ocaml_CFLAGS = \
	$(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_ocaml_LDADD = libtest.la $(LIBGUESTFS_LIBS)

check_SCRIPTS += test-ocaml-plugin.so
test-ocaml-plugin.so: test_ocaml_plugin.ml ../plugins/ocaml/libnbdkitocaml.la ../plugins/ocaml/NBDKit.cmi ../plugins/ocaml/NBDKit.cmx
	$(OCAMLOPT) $(OCAMLOPTFLAGS) -I ../plugins/ocaml \
	  -output-obj -runtime-variant _pic -o $@ \
	  NBDKit.cmx $< \
	  -cclib -L../plugins/ocaml/.libs -cclib -lnbdkitocaml

endif HAVE_OCAML

# perl plugin test.
if HAVE_PERL

check_PROGRAMS += test-perl
TESTS += test-perl

test_perl_SOURCES = test-lang-plugins.c test.h
test_perl_CFLAGS = \
	-DLANG='"perl"' -DSCRIPT='"test.pl"' \
	$(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_perl_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# Shebang test (requires Perl).
TESTS += test-shebang.sh

endif HAVE_PERL

# python plugin test.
if HAVE_PYTHON

check_PROGRAMS += test-python
TESTS += test-python

test_python_SOURCES = test-lang-plugins.c test.h
test_python_CFLAGS = \
	-DLANG='"python"' -DSCRIPT='"test.py"' \
	$(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_python_LDADD = libtest.la $(LIBGUESTFS_LIBS)

endif HAVE_PYTHON

# Ruby plugin test.
if HAVE_RUBY

check_PROGRAMS += test-ruby
TESTS += test-ruby

test_ruby_SOURCES = test-lang-plugins.c test.h
test_ruby_CFLAGS = \
	-DLANG='"ruby"' -DSCRIPT='"test.rb"' \
	$(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_ruby_LDADD = libtest.la $(LIBGUESTFS_LIBS)

endif HAVE_RUBY

# split files plugin test.
check_DATA += split1 split2 split3
MAINTAINERCLEANFILES += split1 split2 split3
split1: file-data
	rm -f $@ $@-t
	dd if=$< of=$@-t bs=1 count=100
	mv $@-t $@
split2: file-data
	rm -f $@ $@-t
	dd if=$< of=$@-t bs=1 count=100 skip=100
	mv $@-t $@
split3: file-data
	rm -f $@ $@-t
	dd if=$< of=$@-t bs=1 skip=200
	mv $@-t $@
check_PROGRAMS += test-split
TESTS += test-split

test_split_SOURCES = test-split.c test.h
test_split_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_split_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# streaming plugin test.
check_PROGRAMS += test-streaming
# Doesn't work:
#TESTS += test-streaming

test_streaming_SOURCES = test-streaming.c test.h
test_streaming_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_streaming_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# xz plugin test.
if HAVE_LIBLZMA
if HAVE_GUESTFISH

check_PROGRAMS += test-xz
TESTS += test-xz
check_DATA += disk.xz
MAINTAINERCLEANFILES += disk.xz

test_xz_SOURCES = test-xz.c test.h
test_xz_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_xz_LDADD = libtest.la $(LIBGUESTFS_LIBS)

disk.xz: disk
	rm -f $@
	xz --best -c disk > $@

endif HAVE_GUESTFISH
endif HAVE_LIBLZMA

endif HAVE_PLUGINS
endif HAVE_LIBGUESTFS
