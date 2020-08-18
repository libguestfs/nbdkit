# nbdkit
# Copyright (C) 2017-2020 Red Hat Inc.
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

EXTRA_DIST = nbdkit-eval-plugin.pod

# Disabled on Windows because no bash scripting.
if !IS_WINDOWS

plugin_LTLIBRARIES = nbdkit-eval-plugin.la

# This plugin shares most of the same sources as nbdkit-sh-plugin.  In
# RHEL 7 we cannot add the C files from ../sh directly to SOURCES
# because subdir-objects is broken.  Instead we create symlinks to
# them.
BUILT_SOURCES = \
	call.c \
	methods.c \
	$(NULL)
call.c: $(srcdir)/../sh/call.c
	ln -f -s $(srcdir)/../sh/$@
methods.c: $(srcdir)/../sh/methods.c
	ln -f -s $(srcdir)/../sh/$@
CLEANFILES += $(BUILT_SOURCES)

nbdkit_eval_plugin_la_SOURCES = \
	eval.c \
	$(BUILT_SOURCES) \
	$(srcdir)/../sh/call.h \
	$(srcdir)/../sh/methods.h \
	$(top_srcdir)/include/nbdkit-plugin.h \
	$(NULL)
nbdkit_eval_plugin_la_CPPFLAGS = \
	-I$(top_srcdir)/plugins/sh \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/common/include \
	-I$(top_srcdir)/common/utils \
	$(NULL)
nbdkit_eval_plugin_la_CFLAGS = $(WARNINGS_CFLAGS)
nbdkit_eval_plugin_la_LIBADD = \
	$(top_builddir)/common/utils/libutils.la \
	$(IMPORT_LIBRARY_ON_WINDOWS) \
	$(NULL)
nbdkit_eval_plugin_la_LDFLAGS = \
	-module -avoid-version -shared $(NO_UNDEFINED_ON_WINDOWS) \
	-Wl,--version-script=$(top_srcdir)/plugins/plugins.syms \
	$(NULL)

if HAVE_POD

man_MANS = nbdkit-eval-plugin.1
CLEANFILES += $(man_MANS)

nbdkit-eval-plugin.1: nbdkit-eval-plugin.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

endif HAVE_POD
endif !IS_WINDOWS
