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

EXTRA_DIST = \
	generate-file-data.sh \
	generate-offset-data.sh \
	make-pki.sh \
	make-psk.sh \
	python-exception.py \
	README.tests \
	shebang.pl \
	shebang.py \
	shebang.rb \
	test-ansi-c.sh \
	test-blocksize.sh \
	test-cache.sh \
	test-cache-max-size.sh \
	test-cache-on-read.sh \
	test-captive.sh \
	test-cow.sh \
	test-cxx.sh \
	test-data-7E.sh \
	test-data-base64.sh \
	test-data-file.sh \
	test-data-raw.sh \
	test-debug-flags.sh \
	test-dump-config.sh \
	test-dump-plugin.sh \
	test-dump-plugin-example4.sh \
	test-eflags.sh \
	test-error0.sh \
	test-error10.sh \
	test-error100.sh \
	test-floppy.sh \
	test-foreground.sh \
	test-fua.sh \
	test-full.sh \
	test-help.sh \
	test-help-plugin.sh \
	test-ip.sh \
	test-iso.sh \
	test-layers.sh \
	test-linuxdisk.sh \
	test-linuxdisk-copy-out.sh \
	test-log.sh \
	test.lua \
	test-memory-largest.sh \
	test-memory-largest-for-qemu.sh \
	test-nozero.sh \
	test_ocaml_plugin.ml \
	test-ocaml.c \
	test-offset2.sh \
	test-parallel-file.sh \
	test-parallel-nbd.sh \
	test-partition1.sh \
	test-partition2.sh \
	test-partitioning1.sh \
	test-partitioning2.sh \
	test-partitioning3.sh \
	test-partitioning4.sh \
	test-partitioning5.sh \
	test-partitioning6.sh \
	test-pattern.sh \
	test-pattern-largest.sh \
	test-pattern-largest-for-qemu.sh \
	test-python-exception.sh \
	test.pl \
	test.py \
	test.rb \
	test.tcl \
	test-shebang-perl.sh \
	test-shebang-python.sh \
	test-shebang-ruby.sh \
	test-shell.sh \
	test-single.sh \
	test-single-from-file.sh \
	test-start.sh \
	test-random-sock.sh \
	test-tls.sh \
	test-tls-psk.sh \
	test-truncate1.sh \
	test-truncate2.sh \
	test-truncate3.sh \
	test-vddk.sh \
	test-version.sh \
	test-version-filter.sh \
	test-version-plugin.sh \
	test-zero.sh

# Use 'make check' to run the ordinary tests.  To run all the tests
# under valgrind, use the following rule:
check-valgrind:
	NBDKIT_VALGRIND=1 $(MAKE) check

# To run only tests which require root, use:
check-root:
	$(MAKE) check TESTS="test-file-block"

#----------------------------------------------------------------------
# Basic server command line and start-up tests.

check_PROGRAMS =
check_DATA =
check_SCRIPTS =
check_LTLIBRARIES =
noinst_LTLIBRARIES =

# Ensure we're testing the local copy by running everything through
# the nbdkit helper script in the top build directory.
TESTS_ENVIRONMENT = PATH=$(abs_top_builddir):$(PATH) SRCDIR=$(srcdir)

TESTS = \
	test-help.sh \
	test-version.sh \
	test-dump-config.sh

if HAVE_PLUGINS

TESTS += \
	test-help-plugin.sh \
	test-version-plugin.sh \
	test-version-filter.sh \
	test-dump-plugin.sh \
	test-start.sh \
	test-single.sh \
	test-single-from-file.sh \
	test-captive.sh \
	test-random-sock.sh \
	test-tls.sh \
	test-tls-psk.sh \
	test-ip.sh \
	test-socket-activation \
	test-foreground.sh \
	test-debug-flags.sh

check_PROGRAMS += \
	test-socket-activation

test_socket_activation_SOURCES = test-socket-activation.c
test_socket_activation_CFLAGS = $(WARNINGS_CFLAGS)

endif HAVE_PLUGINS

if CAN_TEST_ANSI_C
# This builds a plugin using an ANSI (ISO C90) compiler to ensure that
# the header file is compatible.  The plugin does nothing very
# interesting, it's mainly a compile test.
TESTS += \
	test-ansi-c.sh
# check_LTLIBRARIES won't build a shared library (see automake manual).
# So we have to do this and add a dependency.
noinst_LTLIBRARIES += test-ansi-c-plugin.la
test-ansi-c.sh: test-ansi-c-plugin.la

test_ansi_c_plugin_la_SOURCES = \
	test-ansi-c-plugin.c \
	$(top_srcdir)/include/nbdkit-plugin.h
test_ansi_c_plugin_la_CPPFLAGS = \
	-std=c90 -pedantic \
	-I$(top_srcdir)/include
test_ansi_c_plugin_la_CFLAGS = \
        $(WARNINGS_CFLAGS)
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
test_ansi_c_plugin_la_LDFLAGS = \
	-module -avoid-version -shared -rpath /nowhere
endif CAN_TEST_ANSI_C

if HAVE_CXX
# This builds a plugin and a filter using the C++ compiler.  They
# don't do anything interesting when run.
TESTS += \
	test-cxx.sh
# check_LTLIBRARIES won't build a shared library (see automake manual).
# So we have to do this and add a dependency.
noinst_LTLIBRARIES += test-cxx-plugin.la test-cxx-filter.la
test-cxx.sh: test-cxx-plugin.la test-cxx-filter.la

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

test_cxx_filter_la_SOURCES = \
	test-cxx-filter.cpp \
	$(top_srcdir)/include/nbdkit-filter.h
test_cxx_filter_la_CPPFLAGS = \
	-I$(top_srcdir)/include
test_cxx_filter_la_CXXFLAGS = \
        $(WARNINGS_CFLAGS)
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
test_cxx_filter_la_LDFLAGS = \
	-module -avoid-version -shared -rpath /nowhere
endif HAVE_CXX

# Exit with parent test.
check_PROGRAMS += test-exit-with-parent
TESTS += test-exit-with-parent

test_exit_with_parent_SOURCES = \
	test-exit-with-parent.c test.h
test_exit_with_parent_CPPFLAGS = \
	-I$(top_srcdir)/common/include
test_exit_with_parent_CFLAGS = \
	$(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)

# PKI files for the TLS tests.
check_DATA += pki/.stamp
pki/.stamp: $(srcdir)/make-pki.sh
	SRCDIR=$(srcdir) $(srcdir)/make-pki.sh

# PSK keys for the TLS-PSK tests.
check_DATA += keys.psk
keys.psk: $(srcdir)/make-psk.sh
	SRCDIR=$(srcdir) $(srcdir)/make-psk.sh

# Keys are expensive to recreate so only delete them when we do
# ‘make distclean’.
DISTCLEANFILES = keys.psk
distclean-local: distclean-local-tls
distclean-local-tls:
	rm -rf pki

#----------------------------------------------------------------------
# Tests of C plugins or tests which require plugins.

if HAVE_PLUGINS

# Common data shared by multiple tests
check_DATA += file-data
CLEANFILES += file-data
file-data: generate-file-data.sh
	$(srcdir)/generate-file-data.sh $@

# While most tests need libguestfs, testing parallel I/O is easier when
# using qemu-io to kick off asynchronous requests.
TESTS += \
	test-parallel-file.sh \
	test-parallel-nbd.sh

# Most in-depth tests need libguestfs, since that is a convenient way to
# drive qemu.
LIBGUESTFS_TESTS =
EXTRA_PROGRAMS =
if HAVE_LIBGUESTFS
check_PROGRAMS += $(LIBGUESTFS_TESTS)
TESTS += $(LIBGUESTFS_TESTS)

# Use the 'direct' backend, and ensure maximum libguestfs debugging is
# written to the *.log files in case there is a problem.
TESTS_ENVIRONMENT += \
	LIBGUESTFS_ATTACH_METHOD=appliance \
	LIBGUESTFS_DEBUG=1 \
	LIBGUESTFS_TRACE=1 \
	LD_LIBRARY_PATH=../plugins/ocaml/.libs:$(LD_LIBRARY_PATH) \
	LIBRARY_PATH=../plugins/ocaml/.libs:$(LIBRARY_PATH)

# Common test library.
check_LTLIBRARIES += libtest.la
libtest_la_SOURCES = test.c test.h
libtest_la_CFLAGS = $(WARNINGS_CFLAGS)

# Basic connection test.
LIBGUESTFS_TESTS += test-connect

test_connect_SOURCES = test-connect.c test.h
test_connect_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_connect_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# newstyle protocol test.
LIBGUESTFS_TESTS += test-newstyle

test_newstyle_SOURCES = test-newstyle.c test.h
test_newstyle_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_newstyle_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# oldstyle protocol test.
LIBGUESTFS_TESTS += test-oldstyle

test_oldstyle_SOURCES = test-oldstyle.c test.h
test_oldstyle_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_oldstyle_LDADD = libtest.la $(LIBGUESTFS_LIBS)

endif HAVE_LIBGUESTFS

# Test export flags.
TESTS += test-eflags.sh

# common disk image shared with several tests
if HAVE_GUESTFISH
check_DATA += disk
CLEANFILES += disk

disk:
	rm -f $@ test1.img
	guestfish -N fs:ext4:100M -m /dev/sda1 write /hello.txt "hello,world"
	mv test1.img disk

endif HAVE_GUESTFISH

# curl plugin test.

if HAVE_CURL
if HAVE_GUESTFISH

# curl plugin test.
LIBGUESTFS_TESTS += test-curl

test_curl_SOURCES = \
	test-curl.c \
	web-server.c \
	web-server.h \
	test.h
test_curl_CFLAGS = \
	$(WARNINGS_CFLAGS) \
	$(LIBGUESTFS_CFLAGS) \
	$(PTHREAD_CFLAGS)
test_curl_LDFLAGS = \
	$(PTHREAD_LIBS)
test_curl_LDADD = \
	libtest.la \
	$(LIBGUESTFS_LIBS)

endif HAVE_GUESTFISH
endif HAVE_CURL

# data plugin test.
LIBGUESTFS_TESTS += test-data
TESTS += \
	test-data-7E.sh \
	test-data-base64.sh \
	test-data-file.sh \
	test-data-raw.sh

test_data_SOURCES = test-data.c test.h
test_data_CPPFLAGS = -I$(top_srcdir)/common/include
test_data_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_data_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# ext2 plugin test.
if HAVE_EXT2
if HAVE_GUESTFISH

LIBGUESTFS_TESTS += test-ext2
check_DATA += ext2.img
CLEANFILES += ext2.img

ext2.img: disk
	rm -f $@ $@-t
	guestfish \
	    sparse $@-t 2G : \
	    run : \
	    mkfs ext4 /dev/sda : \
	    mount /dev/sda / : \
	    mkdir /disks : \
	    upload $< /disks/disk.img
	mv $@-t $@

test_ext2_SOURCES = test-ext2.c test.h
test_ext2_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_ext2_LDADD = libtest.la $(LIBGUESTFS_LIBS)

endif HAVE_GUESTFISH
endif HAVE_EXT2

# file plugin test.
LIBGUESTFS_TESTS += test-file test-file-block

test_file_SOURCES = test-file.c test.h
test_file_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_file_LDADD = libtest.la $(LIBGUESTFS_LIBS)

test_file_block_SOURCES = test-file-block.c test.h
test_file_block_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_file_block_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# floppy plugin test.
if HAVE_GUESTFISH
TESTS += test-floppy.sh
endif HAVE_GUESTFISH

# full plugin test.
TESTS += test-full.sh

# gzip plugin test.
if HAVE_ZLIB
if HAVE_GUESTFISH

LIBGUESTFS_TESTS += test-gzip
check_DATA += disk.gz
CLEANFILES += disk.gz

test_gzip_SOURCES = test-gzip.c test.h
test_gzip_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_gzip_LDADD = libtest.la $(LIBGUESTFS_LIBS)

disk.gz: disk
	rm -f $@
	gzip -9 -c disk > $@

endif HAVE_GUESTFISH
endif HAVE_ZLIB

# iso plugin test.
if HAVE_ISO
if HAVE_GUESTFISH

TESTS += test-iso.sh

endif HAVE_GUESTFISH
endif HAVE_ISO

# linuxdisk plugin test.
if HAVE_GUESTFISH
TESTS += \
	test-linuxdisk.sh \
	test-linuxdisk-copy-out.sh
endif HAVE_GUESTFISH

# memory plugin test.
LIBGUESTFS_TESTS += test-memory
TESTS += test-memory-largest.sh test-memory-largest-for-qemu.sh

test_memory_SOURCES = test-memory.c test.h
test_memory_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_memory_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# partitioning plugin test.
TESTS += \
	test-partitioning1.sh \
	test-partitioning4.sh \
	test-partitioning6.sh
if HAVE_GUESTFISH
TESTS += \
	test-partitioning2.sh \
	test-partitioning3.sh \
	test-partitioning5.sh
endif HAVE_GUESTFISH

# pattern plugin test.
TESTS += \
	test-pattern.sh \
	test-pattern-largest.sh \
	test-pattern-largest-for-qemu.sh

# nbd plugin test.
LIBGUESTFS_TESTS += test-nbd

test_nbd_SOURCES = test-nbd.c test.h
test_nbd_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_nbd_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# null plugin test.
LIBGUESTFS_TESTS += test-null

test_null_SOURCES = test-null.c test.h
test_null_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_null_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# random plugin test.
LIBGUESTFS_TESTS += test-random

test_random_SOURCES = test-random.c test.h
test_random_CPPFLAGS = -I $(top_srcdir)/common/include
test_random_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_random_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# split files plugin test.
check_DATA += split1 split2 split3
CLEANFILES += split1 split2 split3
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
LIBGUESTFS_TESTS += test-split

test_split_SOURCES = test-split.c test.h
test_split_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_split_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# streaming plugin test.
# Doesn't work:
#LIBGUESTFS_TESTS += test-streaming
EXTRA_PROGRAMS += test-streaming

test_streaming_SOURCES = test-streaming.c test.h
test_streaming_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_streaming_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# VDDK plugin test.
# This only tests that the plugin can be loaded against a
# dummy VDDK library, it is not a detailed test.

# check_LTLIBRARIES won't build a shared library (see automake manual).
# So we have to do this and add a dependency.
noinst_LTLIBRARIES += libvixDiskLib.la
TESTS += test-vddk.sh

libvixDiskLib_la_SOURCES = \
	dummy-vddk.c
libvixDiskLib_la_CPPFLAGS = \
	-I$(top_srcdir)/plugins/vddk
libvixDiskLib_la_CXXFLAGS = \
        $(WARNINGS_CFLAGS)
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
libvixDiskLib_la_LDFLAGS = \
	-shared -version-number 6:0:0 -rpath /nowhere

# zero plugin test.
TESTS += test-zero.sh

#----------------------------------------------------------------------
# Tests of language plugins.

# OCaml plugin test.
if HAVE_OCAML

LIBGUESTFS_TESTS += test-ocaml

# This is somewhat different from the other tests because we have
# to build an actual plugin here.
test_ocaml_SOURCES = test-ocaml.c test.h
test_ocaml_CFLAGS = \
	$(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_ocaml_LDADD = libtest.la $(LIBGUESTFS_LIBS)

check_SCRIPTS += test-ocaml-plugin.so
test-ocaml-plugin.so: test_ocaml_plugin.cmx ../plugins/ocaml/libnbdkitocaml.la ../plugins/ocaml/NBDKit.cmi ../plugins/ocaml/NBDKit.cmx
	$(OCAMLOPT) $(OCAMLOPTFLAGS) -I ../plugins/ocaml \
	  -output-obj -runtime-variant _pic -o $@ \
	  NBDKit.cmx $< \
	  -cclib -L../plugins/ocaml/.libs -cclib -lnbdkitocaml
test_ocaml_plugin.cmx: test_ocaml_plugin.ml ../plugins/ocaml/NBDKit.cmi
	$(OCAMLOPT) $(OCAMLOPTFLAGS) -I ../plugins/ocaml -c $< -o $@

endif HAVE_OCAML

# perl plugin test.
if HAVE_PERL

TESTS += \
	test-dump-plugin-example4.sh \
	test-shebang-perl.sh
LIBGUESTFS_TESTS += test-perl

test_perl_SOURCES = test-lang-plugins.c test.h
test_perl_CFLAGS = \
	-DLANG='"perl"' -DSCRIPT='"$(srcdir)/test.pl"' \
	$(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_perl_LDADD = libtest.la $(LIBGUESTFS_LIBS)

endif HAVE_PERL

# python plugin test.
if HAVE_PYTHON

TESTS += \
	test-python-exception.sh \
	test-shebang-python.sh
LIBGUESTFS_TESTS += test-python

test_python_SOURCES = test-lang-plugins.c test.h
test_python_CFLAGS = \
	-DLANG='"python"' -DSCRIPT='"$(srcdir)/test.py"' \
	$(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_python_LDADD = libtest.la $(LIBGUESTFS_LIBS)

endif HAVE_PYTHON

# Ruby plugin test.
if HAVE_RUBY

# Ruby tests are disabled.  See "WARNING" section in
# plugins/ruby/nbdkit-ruby-plugin.pod

#LIBGUESTFS_TESTS += \
#	test-ruby
#TESTS += test-shebang-ruby.sh
EXTRA_PROGRAMS += test-ruby

test_ruby_SOURCES = test-lang-plugins.c test.h
test_ruby_CFLAGS = \
	-DLANG='"ruby"' -DSCRIPT='"$(srcdir)/test.rb"' \
	$(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_ruby_LDADD = libtest.la $(LIBGUESTFS_LIBS)

endif HAVE_RUBY

# Shell (sh) plugin test.
LIBGUESTFS_TESTS += test-shell
check_DATA += test-shell.img
CLEANFILES += test-shell.img

test_shell_SOURCES = test-lang-plugins.c test.h
test_shell_CFLAGS = \
	-DLANG='"sh"' -DSCRIPT='"$(srcdir)/test-shell.sh"' \
	$(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_shell_LDADD = libtest.la $(LIBGUESTFS_LIBS)

test-shell.img:
	truncate -s 1048576 $@

# Tcl plugin test.
if HAVE_TCL

LIBGUESTFS_TESTS += test-tcl

test_tcl_SOURCES = test-lang-plugins.c test.h
test_tcl_CFLAGS = \
	-DLANG='"tcl"' -DSCRIPT='"$(srcdir)/test.tcl"' \
	$(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_tcl_LDADD = libtest.la $(LIBGUESTFS_LIBS)

endif HAVE_TCL

# Lua plugin test.
if HAVE_LUA

LIBGUESTFS_TESTS += test-lua

test_lua_SOURCES = test-lang-plugins.c test.h
test_lua_CFLAGS = \
	-DLANG='"lua"' -DSCRIPT='"$(srcdir)/test.lua"' \
	$(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_lua_LDADD = libtest.la $(LIBGUESTFS_LIBS)

endif HAVE_LUA

#----------------------------------------------------------------------
# Tests of filters.

# Generic test of filter layers.
check_PROGRAMS += test-layers
TESTS += test-layers.sh test-layers

test_layers_SOURCES = \
	test-layers.c \
	$(top_srcdir)/server/protocol.h
test_layers_CPPFLAGS = \
	-I$(top_srcdir)/common/include \
	-I$(top_srcdir)/server
test_layers_CFLAGS = \
	$(WARNINGS_CFLAGS) \
	$(PTHREAD_CFLAGS)
test_layers_LDFLAGS = \
	$(PTHREAD_LIBS)
test_layers_DEPENDENCIES = \
	test-layers-plugin.la \
	test-layers-filter1.la \
	test-layers-filter2.la \
	test-layers-filter3.la

# check_LTLIBRARIES won't build a shared library (see automake manual).
# So we have to do this and add a dependency.
noinst_LTLIBRARIES += \
	test-layers-plugin.la \
	test-layers-filter1.la \
	test-layers-filter2.la \
	test-layers-filter3.la

test_layers_plugin_la_SOURCES = \
	test-layers-plugin.c \
	$(top_srcdir)/include/nbdkit-plugin.h
test_layers_plugin_la_CPPFLAGS = -I$(top_srcdir)/include
test_layers_plugin_la_CFLAGS = $(WARNINGS_CFLAGS)
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
test_layers_plugin_la_LDFLAGS = \
	-module -avoid-version -shared -rpath /nowhere

test_layers_filter1_la_SOURCES = \
	test-layers-filter.c \
	$(top_srcdir)/include/nbdkit-filter.h
test_layers_filter1_la_CPPFLAGS = -I$(top_srcdir)/include
test_layers_filter1_la_CFLAGS = $(WARNINGS_CFLAGS) -Dlayer='"filter1"'
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
test_layers_filter1_la_LDFLAGS = \
	-module -avoid-version -shared -rpath /nowhere

test_layers_filter2_la_SOURCES = \
	test-layers-filter.c \
	$(top_srcdir)/include/nbdkit-filter.h
test_layers_filter2_la_CPPFLAGS = -I$(top_srcdir)/include
test_layers_filter2_la_CFLAGS = $(WARNINGS_CFLAGS) -Dlayer='"filter2"'
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
test_layers_filter2_la_LDFLAGS = \
	-module -avoid-version -shared -rpath /nowhere

test_layers_filter3_la_SOURCES = \
	test-layers-filter.c \
	$(top_srcdir)/include/nbdkit-filter.h
test_layers_filter3_la_CPPFLAGS = -I$(top_srcdir)/include
test_layers_filter3_la_CFLAGS = $(WARNINGS_CFLAGS) -Dlayer='"filter3"'
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
test_layers_filter3_la_LDFLAGS = \
	-module -avoid-version -shared -rpath /nowhere

# blocksize filter test.
TESTS += test-blocksize.sh

# cache filter test.
if HAVE_GUESTFISH
TESTS += \
	test-cache.sh \
	test-cache-on-read.sh
endif HAVE_GUESTFISH
TESTS += test-cache-max-size.sh

# cow filter test.
if HAVE_GUESTFISH
TESTS += test-cow.sh
endif HAVE_GUESTFISH

# delay filter test.
LIBGUESTFS_TESTS += test-delay

test_delay_SOURCES = test-delay.c test.h
test_delay_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_delay_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# error filter test.
LIBGUESTFS_TESTS += test-error

test_error_SOURCES = test-error.c test.h
test_error_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_error_LDADD = libtest.la $(LIBGUESTFS_LIBS)

TESTS += \
	test-error0.sh \
	test-error10.sh \
	test-error100.sh

# fua filter test.
TESTS += test-fua.sh

# log filter test.
TESTS += test-log.sh

# nozero filter test.
TESTS += test-nozero.sh

# offset filter test.
check_DATA += offset-data
CLEANFILES += offset-data
LIBGUESTFS_TESTS += test-offset

offset-data: generate-offset-data.sh
	$(srcdir)/generate-offset-data.sh $@

test_offset_SOURCES = test-offset.c test.h
test_offset_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_offset_LDADD = libtest.la $(LIBGUESTFS_LIBS)

TESTS += test-offset2.sh

# partition filter test.
TESTS += test-partition1.sh
if HAVE_GUESTFISH
TESTS += test-partition2.sh
endif HAVE_GUESTFISH

# truncate filter tests.
TESTS += \
	test-truncate1.sh \
	test-truncate2.sh \
	test-truncate3.sh

# xz filter test.
if HAVE_LIBLZMA
if HAVE_GUESTFISH

LIBGUESTFS_TESTS += test-xz
check_DATA += disk.xz
CLEANFILES += disk.xz

test_xz_SOURCES = test-xz.c test.h
test_xz_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_xz_LDADD = libtest.la $(LIBGUESTFS_LIBS)

disk.xz: disk
	rm -f $@
# We choose a very small block size here only for testing.  Normally
# you should choose a 16M block size.
	xz --best --block-size=32768 -c disk > $@

if HAVE_CURL

# xz filter + curl.
LIBGUESTFS_TESTS += test-xz-curl

test_xz_curl_SOURCES = \
	test-xz-curl.c \
	web-server.c \
	web-server.h \
	test.h
test_xz_curl_CFLAGS = \
	$(WARNINGS_CFLAGS) \
	$(LIBGUESTFS_CFLAGS) \
	$(PTHREAD_CFLAGS)
test_xz_curl_LDFLAGS = \
	$(PTHREAD_LIBS)
test_xz_curl_LDADD = \
	libtest.la \
	$(LIBGUESTFS_LIBS)

endif HAVE_CURL

endif HAVE_GUESTFISH
endif HAVE_LIBLZMA

endif HAVE_PLUGINS
