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

#----------------------------------------------------------------------
# This directory contains most of the nbdkit tests.
#
# Test variables:
#
#   TESTS            Tests that don't have any special requirements.
#
#   LIBNBD_TESTS     Tests that link to the libnbd C library should be
#                    added to this variable.
#
#   LIBGUESTFS_TESTS Tests that link to libguestfs C library should be
#                    added to this variable.
#
# check_*, noinst_*, EXTRA_DIST, EXTRA_PROGRAMS: These are the usual
# automake variables.  Consult the automake manual for the meaning of
# these.

TESTS =
LIBNBD_TESTS =
LIBGUESTFS_TESTS =

check_PROGRAMS =
check_DATA = functions.sh
check_SCRIPTS =
check_LTLIBRARIES =
noinst_LTLIBRARIES =
EXTRA_DIST = README.tests
EXTRA_PROGRAMS =

# Ensure we're testing the local copy by running everything through
# the nbdkit helper script in the top build directory.
#
# Use the 'direct' backend, and ensure maximum libguestfs debugging.
#
# Enable libnbd debugging.
TESTS_ENVIRONMENT = \
	PATH=$(abs_top_builddir):$(PATH) \
	SRCDIR=$(srcdir) \
	PYTHON=$(PYTHON) \
	LIBGUESTFS_ATTACH_METHOD=appliance \
	LIBGUESTFS_DEBUG=1 \
	LIBGUESTFS_TRACE=1 \
	LIBNBD_DEBUG=1 \
	$(NULL)

#----------------------------------------------------------------------
# Use 'make check' to run the ordinary tests as non-root.  The
# following are special commands for particular scenarios:

# To run all the tests under valgrind, use the following rule:
check-valgrind:
	NBDKIT_VALGRIND=1 $(MAKE) check

# To run only tests which require root, use:
check-root:
	$(MAKE) check TESTS="test-file-block test-swap.sh"

if HAVE_VDDK
# Run a basic check against a real copy of VDDK.  You must set
# vddkdir to point to the library location, eg:
#
#   make check-vddk vddkdir=vmware-vix-disklib-distrib
check-vddk:
	$(MAKE) check TESTS="test-vddk-real.sh test-vddk-real-dump-plugin.sh"
endif HAVE_VDDK

#----------------------------------------------------------------------
# Basic server command line and start-up tests.

TESTS += \
	test-binary.sh \
	test-help.sh \
	test-version.sh \
	test-dump-config.sh \
	test-dump-config-major-1.sh \
	test-dump-config-version-major-minor.sh \
	$(NULL)
EXTRA_DIST += \
	test-binary.sh \
	test-help.sh \
	test-version.sh \
	test-dump-config.sh \
	test-dump-config-major-1.sh \
	test-dump-config-version-major-minor.sh \
	$(NULL)

if HAVE_PLUGINS

TESTS += \
	test-help-example1.sh \
	test-help-plugin.sh \
	test-version-example1.sh \
	test-version-plugin.sh \
	test-version-filter.sh \
	test-dump-plugin-example1.sh \
	test-dump-plugin.sh \
	test-dump-plugin-example2.sh \
	test-dump-plugin-name.sh \
	test-dump-plugin-and-single.sh \
	test-dump-plugin-thread-model.sh \
	test-ddrescue-filter.sh \
	test-probe-filter.sh \
	test-probe-plugin.sh \
	test-start.sh \
	test-single.sh \
	test-single-from-file.sh \
	test-single-sh.sh \
	test-stdio.sh \
	test-captive.sh \
	test-random-sock.sh \
	test-tls.sh \
	test-tls-psk.sh \
	test-ip.sh \
	test-vsock.sh \
	test-socket-activation \
	test-foreground.sh \
	test-debug-flags.sh \
	test-long-name.sh \
	test-flush.sh \
	test-swap.sh \
	test-shutdown.sh \
	test-nbdkit-backend-debug.sh \
	$(NULL)
EXTRA_DIST += \
	test-captive.sh \
	test-ddrescue-filter.sh \
	test-debug-flags.sh \
	test-dump-plugin-and-single.sh \
	test-dump-plugin-example1.sh \
	test-dump-plugin-example2.sh \
	test-dump-plugin-name.sh \
	test-dump-plugin-thread-model.sh \
	test-dump-plugin.sh \
	test-flush.sh \
	test-foreground.sh \
	test-help-example1.sh \
	test-help-plugin.sh \
	test-ip.sh \
	test-long-name.sh \
	test-nbdkit-backend-debug.sh \
	test-probe-filter.sh \
	test-probe-plugin.sh \
	test-random-sock.sh \
	test-shutdown.sh \
	test-single-from-file.sh \
	test-single-sh.sh \
	test-single.sh \
	test-socket-activation \
	test-start.sh \
	test-stdio.sh \
	test-swap.sh \
	test-tls-psk.sh \
	test-tls.sh \
	test-version-example1.sh \
	test-version-filter.sh \
	test-version-plugin.sh \
	test-vsock.sh \
	$(NULL)

check_PROGRAMS += \
	test-socket-activation \
	$(NULL)

test_socket_activation_SOURCES = test-socket-activation.c
test_socket_activation_CPPFLAGS = \
	-I$(top_srcdir)/common/include \
	-I$(top_srcdir)/common/protocol \
	$(NULL)
test_socket_activation_CFLAGS = $(WARNINGS_CFLAGS)

# check_LTLIBRARIES won't build a shared library (see automake manual).
# So we have to do this and add a dependency.
noinst_LTLIBRARIES += \
	test-stdio-plugin.la \
	$(NULL)
test-stdio.sh: test-stdio-plugin.la

test_stdio_plugin_la_SOURCES = \
	test-stdio-plugin.c \
	$(top_srcdir)/include/nbdkit-plugin.h \
	$(NULL)
test_stdio_plugin_la_CPPFLAGS = -I$(top_srcdir)/include
test_stdio_plugin_la_CFLAGS = $(WARNINGS_CFLAGS)
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
test_stdio_plugin_la_LDFLAGS = \
	-module -avoid-version -shared $(SHARED_LDFLAGS) -rpath /nowhere \
	$(NULL)

# check_LTLIBRARIES won't build a shared library (see automake manual).
# So we have to do this and add a dependency.
noinst_LTLIBRARIES += \
	test-flush-plugin.la \
	$(NULL)
test-flush.sh: test-flush-plugin.la

test_flush_plugin_la_SOURCES = \
	test-flush-plugin.c \
	$(top_srcdir)/include/nbdkit-plugin.h \
	$(NULL)
test_flush_plugin_la_CPPFLAGS = -I$(top_srcdir)/include
test_flush_plugin_la_CFLAGS = $(WARNINGS_CFLAGS)
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
test_flush_plugin_la_LDFLAGS = \
	-module -avoid-version -shared $(SHARED_LDFLAGS) -rpath /nowhere \
	$(NULL)

# check_LTLIBRARIES won't build a shared library (see automake manual).
# So we have to do this and add a dependency.
noinst_LTLIBRARIES += \
	test-shutdown-plugin.la \
	$(NULL)
test-shutdown.sh: test-shutdown-plugin.la

test_shutdown_plugin_la_SOURCES = \
	test-shutdown-plugin.c \
	$(top_srcdir)/include/nbdkit-plugin.h \
	$(NULL)
test_shutdown_plugin_la_CPPFLAGS = -I$(top_srcdir)/include
test_shutdown_plugin_la_CFLAGS = $(WARNINGS_CFLAGS)
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
test_shutdown_plugin_la_LDFLAGS = \
	-module -avoid-version -shared $(SHARED_LDFLAGS) -rpath /nowhere \
	$(NULL)

endif HAVE_PLUGINS

# Test the header files can be included on their own.
check_PROGRAMS += \
	test-just-plugin-header \
	test-just-filter-header
	$(NULL)
TESTS += \
	test-just-plugin-header \
	test-just-filter-header
	$(NULL)

test_just_plugin_header_SOURCES = \
	test-just-plugin-header.c \
	$(NULL)
test_just_plugin_header_CPPFLAGS = \
	-I$(top_srcdir)/include \
	$(NULL)
test_just_plugin_header_CFLAGS = $(WARNINGS_CFLAGS)

test_just_filter_header_SOURCES = \
	test-just-filter-header.c \
	$(NULL)
test_just_filter_header_CPPFLAGS = \
	-I$(top_srcdir)/include \
	$(NULL)
test_just_filter_header_CFLAGS = $(WARNINGS_CFLAGS)

if CAN_TEST_ANSI_C
# This builds a plugin using an ANSI (ISO C90) compiler to ensure that
# the header file is compatible.  The plugin does nothing very
# interesting, it's mainly a compile test.
TESTS += test-ansi-c.sh
EXTRA_DIST += test-ansi-c.sh
# check_LTLIBRARIES won't build a shared library (see automake manual).
# So we have to do this and add a dependency.
noinst_LTLIBRARIES += test-ansi-c-plugin.la
test-ansi-c.sh: test-ansi-c-plugin.la

test_ansi_c_plugin_la_SOURCES = \
	test-ansi-c-plugin.c \
	$(top_srcdir)/include/nbdkit-plugin.h \
	$(NULL)
test_ansi_c_plugin_la_CPPFLAGS = \
	-std=c90 -pedantic \
	-I$(top_srcdir)/include \
	$(NULL)
test_ansi_c_plugin_la_CFLAGS = $(WARNINGS_CFLAGS)
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
test_ansi_c_plugin_la_LDFLAGS = \
	-module -avoid-version -shared $(SHARED_LDFLAGS) -rpath /nowhere \
	$(NULL)
endif CAN_TEST_ANSI_C

if HAVE_CXX
# This builds a plugin and a filter using the C++ compiler.  They
# don't do anything interesting when run.
TESTS += test-cxx.sh
EXTRA_DIST += test-cxx.sh
# check_LTLIBRARIES won't build a shared library (see automake manual).
# So we have to do this and add a dependency.
noinst_LTLIBRARIES += test-cxx-plugin.la test-cxx-filter.la
test-cxx.sh: test-cxx-plugin.la test-cxx-filter.la

test_cxx_plugin_la_SOURCES = \
	test-cxx-plugin.cpp \
	$(top_srcdir)/include/nbdkit-plugin.h \
	$(NULL)
test_cxx_plugin_la_CPPFLAGS = \
	-I$(top_srcdir)/include \
	$(NULL)
test_cxx_plugin_la_CXXFLAGS = $(WARNINGS_CFLAGS)
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
test_cxx_plugin_la_LDFLAGS = \
	-module -avoid-version -shared $(SHARED_LDFLAGS) -rpath /nowhere \
	$(NULL)

test_cxx_filter_la_SOURCES = \
	test-cxx-filter.cpp \
	$(top_srcdir)/include/nbdkit-filter.h \
	$(NULL)
test_cxx_filter_la_CPPFLAGS = \
	-I$(top_srcdir)/include \
	$(NULL)
test_cxx_filter_la_CXXFLAGS = $(WARNINGS_CFLAGS)
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
test_cxx_filter_la_LDFLAGS = \
	-module -avoid-version -shared $(SHARED_LDFLAGS) -rpath /nowhere \
	$(NULL)
endif HAVE_CXX

# Exit with parent test.
check_PROGRAMS += test-exit-with-parent
TESTS += test-exit-with-parent

test_exit_with_parent_SOURCES = \
	test-exit-with-parent.c test.h \
	$(NULL)
test_exit_with_parent_CPPFLAGS = \
	-I$(top_srcdir)/common/include \
	$(NULL)
test_exit_with_parent_CFLAGS = \
	$(WARNINGS_CFLAGS) \
	$(LIBGUESTFS_CFLAGS) \
	$(NULL)

# PKI files for the TLS tests.
check_DATA += pki/.stamp
EXTRA_DIST += make-pki.sh
pki/.stamp: $(srcdir)/make-pki.sh
	SRCDIR=$(srcdir) $(srcdir)/make-pki.sh

# PSK keys for the TLS-PSK tests.
check_DATA += keys.psk
EXTRA_DIST += make-psk.sh
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
EXTRA_DIST += generate-file-data.sh
file-data: generate-file-data.sh
	$(srcdir)/generate-file-data.sh $@

# While most tests need libguestfs, testing parallel I/O is easier when
# using qemu-io to kick off asynchronous requests.
TESTS += \
	test-parallel-file.sh \
	test-parallel-nbd.sh \
	test-parallel-sh.sh \
	$(NULL)
EXTRA_DIST += \
	test-parallel-file.sh \
	test-parallel-nbd.sh \
	test-parallel-sh.sh \
	$(NULL)

# Common test library.
check_LTLIBRARIES += libtest.la
libtest_la_SOURCES = test.c test.h
libtest_la_CFLAGS = $(WARNINGS_CFLAGS)

# Basic connection test.
LIBNBD_TESTS += test-connect

test_connect_SOURCES = test-connect.c
test_connect_CFLAGS = $(WARNINGS_CFLAGS) $(LIBNBD_CFLAGS)
test_connect_LDADD = $(LIBNBD_LIBS)

# newstyle protocol test.
LIBNBD_TESTS += test-newstyle

test_newstyle_SOURCES = test-newstyle.c
test_newstyle_CFLAGS = $(WARNINGS_CFLAGS) $(LIBNBD_CFLAGS)
test_newstyle_LDADD = $(LIBNBD_LIBS)

# oldstyle protocol test.
LIBNBD_TESTS += test-oldstyle

test_oldstyle_SOURCES = test-oldstyle.c
test_oldstyle_CFLAGS = $(WARNINGS_CFLAGS) $(LIBNBD_CFLAGS)
test_oldstyle_LDADD = $(LIBNBD_LIBS)

# Test export flags.
TESTS += test-eflags.sh
EXTRA_DIST += test-eflags.sh

# Test export name.
TESTS += test-export-name.sh
EXTRA_DIST += test-export-name.sh

# common disk image shared with several tests
if HAVE_MKE2FS_WITH_D
check_DATA += disk
CLEANFILES += disk

disk:
	rm -rf disk.tmp $@ $@-t
	mkdir disk.tmp
	echo -n "hello,world" > disk.tmp/hello.txt
	$(top_builddir)/nbdkit -fv -U - linuxdisk disk.tmp size=100M \
	    --run 'qemu-img convert $$nbd $@-t'
	rm -rf disk.tmp
	mv $@-t $@
endif

# curl plugin test.
if HAVE_MKE2FS_WITH_D
if HAVE_CURL
TESTS += test-curl-file.sh
EXTRA_DIST += test-curl-file.sh
LIBGUESTFS_TESTS += test-curl

test_curl_SOURCES = \
	test-curl.c \
	web-server.c \
	web-server.h \
	test.h \
	$(NULL)
test_curl_CPPFLAGS = \
	-I$(top_srcdir)/common/utils
test_curl_CFLAGS = \
	$(WARNINGS_CFLAGS) \
	$(LIBGUESTFS_CFLAGS) \
	$(PTHREAD_CFLAGS) \
	$(NULL)
test_curl_LDFLAGS = \
	$(top_builddir)/common/utils/libutils.la \
	$(PTHREAD_LIBS) \
	$(NULL)
test_curl_LDADD = \
	libtest.la \
	$(LIBGUESTFS_LIBS) \
	$(NULL)
endif HAVE_CURL
endif HAVE_MKE2FS_WITH_D

# data plugin test.
LIBGUESTFS_TESTS += test-data
TESTS += \
	test-data-7E.sh \
	test-data-base64.sh \
	test-data-extents.sh \
	test-data-file.sh \
	test-data-nest.sh \
	test-data-raw.sh \
	$(NULL)
EXTRA_DIST += \
	test-data-7E.sh \
	test-data-base64.sh \
	test-data-extents.sh \
	test-data-file.sh \
	test-data-nest.sh \
	test-data-raw.sh \
	$(NULL)

test_data_SOURCES = test-data.c test.h
test_data_CPPFLAGS = -I$(top_srcdir)/common/include
test_data_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_data_LDADD = libtest.la $(LIBGUESTFS_LIBS)

# eval plugin test.
TESTS += \
	test-eval.sh \
	test-eval-file.sh \
	$(NULL)
EXTRA_DIST += \
	test-eval.sh \
	test-eval-file.sh \
	$(NULL)

# ext2 filter and plugin test.
if HAVE_MKE2FS_WITH_D
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
endif HAVE_MKE2FS_WITH_D

# file plugin test.
LIBNBD_TESTS += test-file
LIBGUESTFS_TESTS += test-file-block

test_file_SOURCES = test-file.c
test_file_CFLAGS = $(WARNINGS_CFLAGS) $(LIBNBD_CFLAGS)
test_file_LDADD = $(LIBNBD_LIBS)

test_file_block_SOURCES = test-file-block.c test.h
test_file_block_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_file_block_LDADD = libtest.la $(LIBGUESTFS_LIBS)

TESTS += test-file-extents.sh
EXTRA_DIST += test-file-extents.sh

# floppy plugin test.
TESTS += test-floppy.sh
EXTRA_DIST += test-floppy.sh

# full plugin test.
TESTS += test-full.sh
EXTRA_DIST += test-full.sh

# gzip plugin test.
if HAVE_MKE2FS_WITH_D
if HAVE_ZLIB
LIBGUESTFS_TESTS += test-gzip
check_DATA += disk.gz
CLEANFILES += disk.gz

test_gzip_SOURCES = test-gzip.c test.h
test_gzip_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_gzip_LDADD = libtest.la $(LIBGUESTFS_LIBS)

disk.gz: disk
	rm -f $@
	gzip -9 -c disk > $@
endif HAVE_ZLIB
endif HAVE_MKE2FS_WITH_D

# info plugin test.
TESTS += \
	test-info-address.sh \
	test-info-base64.sh \
	test-info-raw.sh \
	test-info-time.sh \
	test-info-uptime.sh \
	test-info-conntime.sh \
	$(NULL)
EXTRA_DIST += \
	test-info-address.sh \
	test-info-base64.sh \
	test-info-raw.sh \
	test-info-time.sh \
	test-info-uptime.sh \
	test-info-conntime.sh \
	$(NULL)

# iso plugin test.
if HAVE_ISO
TESTS += test-iso.sh
endif HAVE_ISO
EXTRA_DIST += test-iso.sh

# linuxdisk plugin test.
if HAVE_MKE2FS_WITH_D
TESTS += \
	test-linuxdisk.sh \
	test-linuxdisk-copy-out.sh \
	$(NULL)
endif HAVE_MKE2FS_WITH_D
EXTRA_DIST += \
	test-linuxdisk.sh \
	test-linuxdisk-copy-out.sh \
	$(NULL)

# memory plugin test.
LIBGUESTFS_TESTS += test-memory
TESTS += \
	test-memory-largest.sh \
	test-memory-largest-for-qemu.sh \
	$(NULL)
EXTRA_DIST += \
	test-memory-largest.sh \
	test-memory-largest-for-qemu.sh \
	$(NULL)

test_memory_SOURCES = test-memory.c test.h
test_memory_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_memory_LDADD = libtest.la $(LIBGUESTFS_LIBS)

if HAVE_LIBNBD
# nbd plugin test.
LIBGUESTFS_TESTS += test-nbd
TESTS += \
	test-nbd-extents.sh \
	test-nbd-tls.sh \
	test-nbd-tls-psk.sh \
	$(NULL)
EXTRA_DIST += \
	test-nbd-extents.sh \
	test-nbd-tls.sh \
	test-nbd-tls-psk.sh \
	$(NULL)

test_nbd_SOURCES = test-nbd.c test.h
test_nbd_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_nbd_LDADD = libtest.la $(LIBGUESTFS_LIBS)
endif HAVE_LIBNBD

# null plugin test.
LIBNBD_TESTS += test-null
TESTS += test-null-extents.sh
EXTRA_DIST += test-null-extents.sh

test_null_SOURCES = test-null.c
test_null_CFLAGS = $(WARNINGS_CFLAGS) $(LIBNBD_CFLAGS)
test_null_LDADD = $(LIBNBD_LIBS)

# partitioning plugin test.
TESTS += \
	test-partitioning1.sh \
	test-partitioning2.sh \
	test-partitioning3.sh \
	test-partitioning4.sh \
	test-partitioning5.sh \
	test-partitioning6.sh \
	$(NULL)
EXTRA_DIST += \
	test-partitioning1.sh \
	test-partitioning2.sh \
	test-partitioning3.sh \
	test-partitioning4.sh \
	test-partitioning5.sh \
	test-partitioning6.sh \
	$(NULL)

# pattern plugin test.
TESTS += \
	test-pattern.sh \
	test-pattern-largest.sh \
	test-pattern-largest-for-qemu.sh \
	$(NULL)
EXTRA_DIST += \
	test-pattern.sh \
	test-pattern-largest.sh \
	test-pattern-largest-for-qemu.sh \
	$(NULL)

# random plugin test.
LIBNBD_TESTS += test-random

test_random_SOURCES = test-random.c
test_random_CPPFLAGS = -I $(top_srcdir)/common/include
test_random_CFLAGS = $(WARNINGS_CFLAGS) $(LIBNBD_CFLAGS)
test_random_LDADD = $(LIBNBD_LIBS)

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
LIBNBD_TESTS += test-split

test_split_SOURCES = test-split.c
test_split_CFLAGS = $(WARNINGS_CFLAGS) $(LIBNBD_CFLAGS)
test_split_LDADD = $(LIBNBD_LIBS)

TESTS += test-split-extents.sh
EXTRA_DIST += test-split-extents.sh

# ssh plugin test.
if HAVE_SSH
TESTS += test-ssh.sh
check_DATA += \
	ssh/sshd_config \
	ssh/ssh_host_rsa_key ssh/ssh_host_rsa_key.pub \
	ssh/authorized_keys \
	$(NULL)
EXTRA_DIST += \
	ssh/sshd_config.in \
	test-ssh.sh \
	$(NULL)
SSH_RANDOM_PORT := $(shell bash -c 'echo $$(( 10000 + $$RANDOM ))')
CLEANFILES += \
	ssh/*~ \
	ssh/sshd_config \
	ssh/authorized_keys \
	$(NULL)
DISTCLEANFILES += \
	ssh/ssh_host_rsa_key ssh/ssh_host_rsa_key.pub \
	ssh/id_rsa ssh/id_rsa.pub \
	stamp-ssh-host-key stamp-ssh-user-key \
	$(NULL)

ssh/sshd_config: ssh/sshd_config.in Makefile
	rm -f $@ $@-t
	$(SED) \
	    -e 's,__abs_builddir__,$(abs_builddir),g' \
	    -e 's,__RANDOM_PORT__,$(SSH_RANDOM_PORT),g' \
	    < $< > $@-t
	mv $@-t $@

ssh/ssh_host_rsa_key ssh/ssh_host_rsa_key.pub: stamp-ssh-host-key
stamp-ssh-host-key:
	rm -f $@
	rm -f ssh/ssh_host_rsa_key
	rm -f ssh/ssh_host_rsa_key.pub
	ssh-keygen -t rsa -f ssh/ssh_host_rsa_key -N ''
	touch $@

ssh/authorized_keys: ssh/id_rsa.pub
	rm -f $@ $@-t
	cp $< $@-t
	mv $@-t $@

ssh/id_rsa ssh/id_rsa.pub: stamp-ssh-user-key
stamp-ssh-user-key:
	rm -f $@
	rm -f ssh/id_rsa
	rm -f ssh/id_rsa.pub
	ssh-keygen -t rsa -f ssh/id_rsa -N ''
	touch $@
endif HAVE_SSH

# streaming plugin test.
# Doesn't work:
LIBNBD_TESTS += test-streaming

test_streaming_SOURCES = test-streaming.c
test_streaming_CFLAGS = $(WARNINGS_CFLAGS) $(LIBNBD_CFLAGS)
test_streaming_LDADD = $(LIBNBD_LIBS)

# tar plugin test (written in perl).
if HAVE_PERL
TESTS += test-tar.sh
endif HAVE_PERL
EXTRA_DIST += test-tar.sh

# tmpdisk plugin test.
LIBGUESTFS_TESTS += test-tmpdisk
TESTS += test-tmpdisk-command.sh
EXTRA_DIST += test-tmpdisk-command.sh

test_tmpdisk_SOURCES = \
	test-tmpdisk.c \
	test.h \
	$(NULL)
test_tmpdisk_CPPFLAGS = \
	-I$(top_srcdir)/common/utils
test_tmpdisk_CFLAGS = \
	$(WARNINGS_CFLAGS) \
	$(LIBGUESTFS_CFLAGS) \
	$(NULL)
test_tmpdisk_LDFLAGS = \
	$(top_builddir)/common/utils/libutils.la \
	$(NULL)
test_tmpdisk_LDADD = \
	libtest.la \
	$(LIBGUESTFS_LIBS) \
	$(NULL)

if HAVE_VDDK
# VDDK plugin test.
# This only tests that the plugin can be loaded against a
# dummy VDDK library, it is not a detailed test.

# check_LTLIBRARIES won't build a shared library (see automake manual).
# So we have to do this and add a dependency.
noinst_LTLIBRARIES += libvixDiskLib.la
TESTS += \
	test-vddk.sh \
	test-vddk-dump-plugin.sh \
	test-vddk-real.sh \
	test-vddk-real-dump-plugin.sh \
	$(NULL)

libvixDiskLib_la_SOURCES = \
	dummy-vddk.c \
	$(NULL)
libvixDiskLib_la_CPPFLAGS = \
	-I$(top_srcdir)/plugins/vddk \
	$(NULL)
libvixDiskLib_la_CXXFLAGS = $(WARNINGS_CFLAGS)
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
libvixDiskLib_la_LDFLAGS = \
	-shared -version-number 6:0:0 -no-undefined -rpath /nowhere \
	$(NULL)
endif HAVE_VDDK

EXTRA_DIST += \
	test-vddk.sh \
	test-vddk-dump-plugin.sh \
	test-vddk-real.sh \
	test-vddk-real-dump-plugin.sh \
	$(NULL)

# zero plugin test.
TESTS += test-zero.sh
EXTRA_DIST += test-zero.sh

#----------------------------------------------------------------------
# Tests of language plugins.

# OCaml plugin test.
if HAVE_OCAML

LIBGUESTFS_TESTS += test-ocaml

# This is somewhat different from the other tests because we have
# to build an actual plugin here.
test_ocaml_SOURCES = test-ocaml.c test.h
test_ocaml_CFLAGS = \
	$(WARNINGS_CFLAGS) \
	$(LIBGUESTFS_CFLAGS) \
	$(NULL)
test_ocaml_LDADD = libtest.la $(LIBGUESTFS_LIBS)

check_SCRIPTS += test-ocaml-plugin.so
test-ocaml-plugin.so: test_ocaml_plugin.cmx ../plugins/ocaml/libnbdkitocaml.la ../plugins/ocaml/NBDKit.cmi ../plugins/ocaml/NBDKit.cmx
	$(OCAMLOPT) $(OCAMLOPTFLAGS) -I ../plugins/ocaml \
	  -output-obj -runtime-variant _pic -o $@ \
	  NBDKit.cmx $< \
	  -cclib -L../plugins/ocaml/.libs -cclib -lnbdkitocaml
test_ocaml_plugin.cmx: test_ocaml_plugin.ml ../plugins/ocaml/NBDKit.cmi ../plugins/ocaml/NBDKit.cmx
	$(OCAMLOPT) $(OCAMLOPTFLAGS) -I ../plugins/ocaml -c $< -o $@

endif HAVE_OCAML

EXTRA_DIST += \
	test_ocaml_plugin.ml \
	test-ocaml.c \
	$(NULL)

# perl plugin test.
if HAVE_PERL

TESTS += \
	test-dump-plugin-example4.sh \
	test-shebang-perl.sh \
	$(NULL)
EXTRA_DIST += \
	shebang.pl \
	test.pl \
	test-dump-plugin-example4.sh \
	test-shebang-perl.sh \
	$(NULL)
LIBGUESTFS_TESTS += test-perl

test_perl_SOURCES = test-lang-plugins.c test.h
test_perl_CFLAGS = \
	-DLANG='"perl"' -DSCRIPT='"$(srcdir)/test.pl"' \
	$(WARNINGS_CFLAGS) \
	$(LIBGUESTFS_CFLAGS) \
	$(NULL)
test_perl_LDADD = libtest.la $(LIBGUESTFS_LIBS)

endif HAVE_PERL

# python plugin test.
if HAVE_PYTHON

TESTS += \
	test-python.sh \
	test-python-exception.sh \
	test-shebang-python.sh \
	$(NULL)
EXTRA_DIST += \
	python-exception.py \
	shebang.py \
	test-python-exception.sh \
	test-python-plugin.py \
	test-python.sh \
	test-shebang-python.sh \
	test_python.py \
	$(NULL)

endif HAVE_PYTHON

# Ruby plugin test.
if HAVE_RUBY

# Ruby tests are disabled.  See "WARNING" section in
# plugins/ruby/nbdkit-ruby-plugin.pod

#LIBGUESTFS_TESTS += \
#	test-ruby \
#	$(NULL)
#TESTS += test-shebang-ruby.sh
EXTRA_PROGRAMS += test-ruby
EXTRA_DIST += \
	shebang.rb \
	test.rb \
	test-shebang-ruby.sh \
	$(NULL)

test_ruby_SOURCES = test-lang-plugins.c test.h
test_ruby_CFLAGS = \
	-DLANG='"ruby"' -DSCRIPT='"$(srcdir)/test.rb"' \
	$(WARNINGS_CFLAGS) \
	$(LIBGUESTFS_CFLAGS) \
	$(NULL)
test_ruby_LDADD = libtest.la $(LIBGUESTFS_LIBS)

endif HAVE_RUBY

# Shell (sh) plugin test.
LIBGUESTFS_TESTS += test-shell
check_DATA += test-shell.img
EXTRA_DIST += test-shell.sh
CLEANFILES += test-shell.img

test_shell_SOURCES = test-lang-plugins.c test.h
test_shell_CFLAGS = \
	-DLANG='"sh"' -DSCRIPT='"$(srcdir)/test-shell.sh"' \
	$(WARNINGS_CFLAGS) \
	$(LIBGUESTFS_CFLAGS) \
	$(NULL)
test_shell_LDADD = libtest.la $(LIBGUESTFS_LIBS)

test-shell.img:
	truncate -s 1048576 $@

TESTS += \
	test-sh-errors.sh \
	test-sh-extents.sh \
	test-sh-tmpdir-leak.sh \
	$(NULL)
EXTRA_DIST += \
	test-sh-errors.sh \
	test-sh-extents.sh \
	test-sh-tmpdir-leak.sh \
	$(NULL)

# Tcl plugin test.
if HAVE_TCL

LIBGUESTFS_TESTS += test-tcl
EXTRA_DIST += test.tcl

test_tcl_SOURCES = test-lang-plugins.c test.h
test_tcl_CFLAGS = \
	-DLANG='"tcl"' -DSCRIPT='"$(srcdir)/test.tcl"' \
	$(WARNINGS_CFLAGS) \
	$(LIBGUESTFS_CFLAGS) \
	$(NULL)
test_tcl_LDADD = libtest.la $(LIBGUESTFS_LIBS)

endif HAVE_TCL

# Lua plugin test.
if HAVE_LUA

LIBGUESTFS_TESTS += test-lua
EXTRA_DIST += test.lua

test_lua_SOURCES = test-lang-plugins.c test.h
test_lua_CFLAGS = \
	-DLANG='"lua"' -DSCRIPT='"$(srcdir)/test.lua"' \
	$(WARNINGS_CFLAGS) \
	$(LIBGUESTFS_CFLAGS) \
	$(NULL)
test_lua_LDADD = libtest.la $(LIBGUESTFS_LIBS)

endif HAVE_LUA

# Golang plugin test.
if HAVE_GOLANG

LIBGUESTFS_TESTS += test-golang

test_golang_SOURCES = test-golang.c test.h
test_golang_CFLAGS = \
	$(WARNINGS_CFLAGS) \
	$(LIBGUESTFS_CFLAGS) \
	$(NULL)
test_golang_LDADD = libtest.la $(LIBGUESTFS_LIBS)

endif HAVE_GOLANG

#----------------------------------------------------------------------
# Tests of filters.

# Generic test of filter layers.
check_PROGRAMS += test-layers
TESTS += \
	test-layers.sh \
	test-layers \
	$(NULL)
EXTRA_DIST += test-layers.sh

test_layers_SOURCES = \
	test-layers.c \
	$(NULL)
test_layers_CPPFLAGS = \
	-I$(top_srcdir)/common/include \
	-I$(top_srcdir)/common/protocol \
	-I$(top_srcdir)/common/utils \
	-I$(top_srcdir)/server \
	$(NULL)
test_layers_CFLAGS = \
	$(WARNINGS_CFLAGS) \
	$(PTHREAD_CFLAGS) \
	$(NULL)
test_layers_LDFLAGS = \
	$(PTHREAD_LIBS) \
	$(NULL)
test_layers_LDADD = \
	$(top_builddir)/common/utils/libutils.la \
	$(NULL)
EXTRA_test_layers_DEPENDENCIES = \
	test-layers-plugin.la \
	test-layers-filter1.la \
	test-layers-filter2.la \
	test-layers-filter3.la \
	$(NULL)

# check_LTLIBRARIES won't build a shared library (see automake manual).
# So we have to do this and add a dependency.
noinst_LTLIBRARIES += \
	test-layers-plugin.la \
	test-layers-filter1.la \
	test-layers-filter2.la \
	test-layers-filter3.la \
	$(NULL)

test_layers_plugin_la_SOURCES = \
	test-layers-plugin.c \
	$(top_srcdir)/include/nbdkit-plugin.h \
	$(NULL)
test_layers_plugin_la_CPPFLAGS = -I$(top_srcdir)/include
test_layers_plugin_la_CFLAGS = $(WARNINGS_CFLAGS)
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
test_layers_plugin_la_LDFLAGS = \
	-module -avoid-version -shared $(SHARED_LDFLAGS) -rpath /nowhere \
	$(NULL)

test_layers_filter1_la_SOURCES = \
	test-layers-filter.c \
	$(top_srcdir)/include/nbdkit-filter.h \
	$(NULL)
test_layers_filter1_la_CPPFLAGS = -I$(top_srcdir)/include
test_layers_filter1_la_CFLAGS = $(WARNINGS_CFLAGS) -Dlayer='"filter1"'
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
test_layers_filter1_la_LDFLAGS = \
	-module -avoid-version -shared $(SHARED_LDFLAGS) -rpath /nowhere \
	$(NULL)

test_layers_filter2_la_SOURCES = \
	test-layers-filter.c \
	$(top_srcdir)/include/nbdkit-filter.h \
	$(NULL)
test_layers_filter2_la_CPPFLAGS = -I$(top_srcdir)/include
test_layers_filter2_la_CFLAGS = $(WARNINGS_CFLAGS) -Dlayer='"filter2"'
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
test_layers_filter2_la_LDFLAGS = \
	-module -avoid-version -shared $(SHARED_LDFLAGS) -rpath /nowhere \
	$(NULL)

test_layers_filter3_la_SOURCES = \
	test-layers-filter.c \
	$(top_srcdir)/include/nbdkit-filter.h \
	$(NULL)
test_layers_filter3_la_CPPFLAGS = -I$(top_srcdir)/include
test_layers_filter3_la_CFLAGS = $(WARNINGS_CFLAGS) -Dlayer='"filter3"'
# For use of the -rpath option, see:
# https://lists.gnu.org/archive/html/libtool/2007-07/msg00067.html
test_layers_filter3_la_LDFLAGS = \
	-module -avoid-version -shared $(SHARED_LDFLAGS) -rpath /nowhere \
	$(NULL)

# blocksize filter test.
TESTS += test-blocksize.sh
EXTRA_DIST += test-blocksize.sh

# cache filter test.
TESTS += \
	test-cache.sh \
	test-cache-on-read.sh \
	test-cache-max-size.sh \
	$(NULL)
EXTRA_DIST += \
	test-cache.sh \
	test-cache-on-read.sh \
	test-cache-max-size.sh \
	$(NULL)

# cacheextents filter test.
TESTS += test-cacheextents.sh
EXTRA_DIST += test-cacheextents.sh

# cow filter test.
if HAVE_MKE2FS_WITH_D
TESTS += test-cow.sh
endif
TESTS += test-cow-null.sh
EXTRA_DIST += \
	test-cow.sh \
	test-cow-null.sh \
	$(NULL)

# delay filter tests.
TESTS += test-delay-shutdown.sh
EXTRA_DIST += test-delay-shutdown.sh
LIBNBD_TESTS += test-delay

test_delay_SOURCES = test-delay.c
test_delay_CFLAGS = $(WARNINGS_CFLAGS) $(LIBNBD_CFLAGS)
test_delay_LDADD = $(LIBNBD_LIBS)

# error filter test.
TESTS += \
	test-error0.sh \
	test-error10.sh \
	test-error100.sh \
	test-error-triggered.sh \
	$(NULL)
EXTRA_DIST += \
	test-error0.sh \
	test-error10.sh \
	test-error100.sh \
	test-error-triggered.sh \
	$(NULL)

# exitlast filter test.
TESTS += test-exitlast.sh
EXTRA_DIST += test-exitlast.sh

# extentlist filter test.
TESTS += test-extentlist.sh
EXTRA_DIST += test-extentlist.sh

# fua filter test.
TESTS += test-fua.sh
EXTRA_DIST += test-fua.sh

# ip filter test.
TESTS += test-ip-filter.sh
EXTRA_DIST += test-ip-filter.sh

# limit filter test.
TESTS += test-limit.sh
EXTRA_DIST += test-limit.sh

# log filter test.
TESTS += test-log.sh
EXTRA_DIST += test-log.sh

# nofilter test.
TESTS += test-nofilter.sh
EXTRA_DIST += test-nofilter.sh

if HAVE_LIBNBD
# nozero filter test.
TESTS += test-nozero.sh
endif HAVE_LIBNBD
EXTRA_DIST += test-nozero.sh

# offset filter test.
LIBGUESTFS_TESTS += test-offset

test_offset_SOURCES = test-offset.c test.h
test_offset_CFLAGS = $(WARNINGS_CFLAGS) $(LIBGUESTFS_CFLAGS)
test_offset_LDADD = libtest.la $(LIBGUESTFS_LIBS)

TESTS += \
	test-offset2.sh \
	test-offset-extents.sh \
	$(NULL)
EXTRA_DIST += \
	test-offset2.sh \
	test-offset-extents.sh \
	$(NULL)

# offset + truncate test.
TESTS += test-offset-truncate.sh
EXTRA_DIST += test-offset-truncate.sh

# partition filter test.
TESTS += \
	test-partition1.sh \
	test-partition2.sh \
	$(NULL)
EXTRA_DIST += \
	test-partition1.sh \
	test-partition2.sh \
	$(NULL)

# rate filter test.
TESTS += \
	test-rate.sh \
	test-rate-dynamic.sh \
	$(NULL)
EXTRA_DIST += \
	test-rate.sh \
	test-rate-dynamic.sh \
	$(NULL)

# readahead filter test.
TESTS += \
	test-readahead.sh \
	test-readahead-copy.sh \
	$(NULL)
EXTRA_DIST += \
	test-readahead.sh \
	test-readahead-copy.sh \
	test-readahead-test-plugin.sh \
	test-readahead-test-request.py \
	$(NULL)

# retry filter test.
TESTS += \
	test-retry.sh \
	test-retry-readonly.sh \
	test-retry-extents.sh \
	test-retry-size.sh \
	test-retry-reopen-fail.sh \
	test-retry-zero-flags.sh \
	$(NULL)
EXTRA_DIST += \
	test-retry.sh \
	test-retry-readonly.sh \
	test-retry-extents.sh \
	test-retry-size.sh \
	test-retry-reopen-fail.sh \
	test-retry-zero-flags.sh \
	$(NULL)

# truncate filter tests.
TESTS += \
	test-truncate1.sh \
	test-truncate2.sh \
	test-truncate3.sh \
	test-truncate4.sh \
	test-truncate-extents.sh \
	$(NULL)
EXTRA_DIST += \
	test-truncate1.sh \
	test-truncate2.sh \
	test-truncate3.sh \
	test-truncate4.sh \
	test-truncate-extents.sh \
	$(NULL)

# xz filter test.
if HAVE_MKE2FS_WITH_D
if HAVE_LIBLZMA
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

# xz filter + curl.
if HAVE_CURL
LIBGUESTFS_TESTS += test-xz-curl

test_xz_curl_SOURCES = \
	test-xz-curl.c \
	web-server.c \
	web-server.h \
	test.h \
	$(NULL)
test_xz_curl_CPPFLAGS = \
	-I$(top_srcdir)/common/utils
test_xz_curl_CFLAGS = \
	$(WARNINGS_CFLAGS) \
	$(LIBGUESTFS_CFLAGS) \
	$(PTHREAD_CFLAGS) \
	$(NULL)
test_xz_curl_LDFLAGS = \
	$(top_builddir)/common/utils/libutils.la \
	$(PTHREAD_LIBS) \
	$(NULL)
test_xz_curl_LDADD = \
	libtest.la \
	$(LIBGUESTFS_LIBS) \
	$(NULL)

endif HAVE_CURL
endif HAVE_LIBLZMA
endif HAVE_MKE2FS_WITH_D

endif HAVE_PLUGINS

#----------------------------------------------------------------------
# Tests of old plugins.

TESTS += \
	test-old-plugins-i686-Linux-v1.0.0.sh \
	test-old-plugins-i686-Linux-v1.2.8-2-gd2d934df.sh \
	test-old-plugins-i686-Linux-v1.8.4-3-g11f5a90d.sh \
	test-old-plugins-i686-Linux-v1.12.8-2-g1e2ccc27.sh \
	test-old-plugins-i686-Linux-v1.18.4.sh \
	test-old-plugins-x86_64-Linux-v1.0.0.sh \
	test-old-plugins-x86_64-Linux-v1.2.8.sh \
	test-old-plugins-x86_64-Linux-v1.8.4.sh \
	test-old-plugins-x86_64-Linux-v1.12.8.sh \
	test-old-plugins-x86_64-Linux-v1.18.2.sh \
	$(NULL)

test-old-plugins-%.sh:
	rm -f $@ $@-t
	f=`echo "$@" | $(SED) 's/test-old-plugins-\(.*\).sh/\1/'`; \
	echo 'script=$@ exec $$srcdir/test-old-plugins.sh' "$$f" > $@-t
	chmod 0755 $@-t
	mv $@-t $@

EXTRA_DIST += \
	old-plugins/README \
	old-plugins/*/*/*/nbdkit-file-plugin.so \
	test-old-plugins.sh \
	$(NULL)
CLEANFILES += \
	test-old-plugins-*.sh \
	$(NULL)

#----------------------------------------------------------------------

if HAVE_LIBNBD
check_PROGRAMS += $(LIBNBD_TESTS)
TESTS += $(LIBNBD_TESTS)
endif HAVE_LIBNBD

if HAVE_LIBGUESTFS
check_PROGRAMS += $(LIBGUESTFS_TESTS)
TESTS += $(LIBGUESTFS_TESTS)
endif HAVE_LIBGUESTFS
