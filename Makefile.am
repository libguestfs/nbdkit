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

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = \
	BENCHMARKING \
	.dir-locals.el \
	.gitignore \
	html/pod.css \
	LICENSE \
	m4/.gitignore \
	OTHER_PLUGINS \
	scripts/git.orderfile \
	SECURITY \
	$(NULL)

CLEANFILES += html/*.html

if !ENABLE_LIBFUZZER
# NB: This is not the real nbdkit binary.  It's a wrapper that allows
# you to run nbdkit from the build directory before it is installed.
noinst_PROGRAMS = nbdkit
nbdkit_SOURCES = wrapper.c server/options.h
nbdkit_CPPFLAGS = \
	-I$(top_srcdir)/server \
	-I$(top_srcdir)/common/utils \
	-Dbuilddir=\"$(abs_top_builddir)\" \
	-Dsrcdir=\"$(abs_top_srcdir)\" \
	-DVALGRIND=\"$(VALGRIND)\" \
	$(NULL)
nbdkit_CFLAGS = $(WARNINGS_CFLAGS)
nbdkit_LDADD = \
	$(top_builddir)/common/utils/libutils.la \
	$(NULL)
EXTRA_nbdkit_DEPENDENCIES = config.status
endif !ENABLE_LIBFUZZER

SUBDIRS = \
	bash \
	docs \
	fuzzing \
	valgrind \
	include \
	common/include \
	common/protocol \
	common/replacements \
	common/utils \
	server \
	$(NULL)

if HAVE_PLUGINS
SUBDIRS += \
	common/allocators \
	common/bitmap \
	common/gpt \
	common/regions \
	plugins \
	filters \
	$(NULL)
endif

SUBDIRS += . tests

check-valgrind:
	$(MAKE) -C tests check-valgrind

check-root:
	$(MAKE) -C tests check-root

check-vddk:
	$(MAKE) -C tests check-vddk

#----------------------------------------------------------------------
# Maintainers only!

# Check no files are missing from EXTRA_DIST rules, and that all
# generated files have been included in the tarball.  (Note you must
# have done 'make dist')
maintainer-check-extra-dist:
	@zcat $(PACKAGE_NAME)-$(VERSION).tar.gz | tar tf - | sort | \
	  sed 's,^$(PACKAGE_NAME)-$(VERSION)/,,' > tarfiles
	@git ls-files | \
	  sort > gitfiles
	@comm -13 tarfiles gitfiles > comm-out
	@echo Checking for differences between EXTRA_DIST and git ...
	@cat comm-out
	@[ ! -s comm-out ]
	@rm tarfiles gitfiles comm-out
	@echo PASS: EXTRA_DIST tests

# Commit everything in the current directory and set the commit
# message to the current version number.
maintainer-commit:
	git commit -a -m "Version $(VERSION)."

# Tag HEAD with the current version.
maintainer-tag:
	git tag -a v$(VERSION) -m "Version $(VERSION)." -f

# Run tests and display the time taken to run each test, which is
# useful for diagnosing slow-running tests.
#
# Try also setting LIBGUESTFS_BACKEND_SETTINGS=force_tcg which
# emulates what happens in Koji.
maintainer-check-times:
	make check -j1 | ts -i
