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

EXTRA_DIST = \
	make-links.sh \
	nbdkit.pod \
	nbdkit-captive.pod \
	nbdkit-client.pod \
	nbdkit-loop.pod \
	nbdkit-probing.pod \
	nbdkit-protocol.pod \
	nbdkit-release-notes-1.4.pod \
	nbdkit-release-notes-1.6.pod \
	nbdkit-release-notes-1.8.pod \
	nbdkit-release-notes-1.10.pod \
	nbdkit-release-notes-1.12.pod \
	nbdkit-release-notes-1.14.pod \
	nbdkit-release-notes-1.16.pod \
	nbdkit-release-notes-1.18.pod \
	nbdkit-release-notes-1.20.pod \
	nbdkit-release-notes-1.22.pod \
	nbdkit-release-notes-1.24.pod \
	nbdkit-release-notes-1.26.pod \
	nbdkit-release-notes-1.28.pod \
	nbdkit-security.pod \
	nbdkit-service.pod \
	nbdkit-tls.pod \
	nbdkit-plugin.pod \
	nbdkit-filter.pod \
	synopsis.txt \
	$(NULL)

if HAVE_POD

man_MANS = \
	nbdkit.1 \
	nbdkit-captive.1 \
	nbdkit-client.1 \
	nbdkit-loop.1 \
	nbdkit-probing.1 \
	nbdkit-protocol.1 \
	nbdkit-release-notes-1.4.1 \
	nbdkit-release-notes-1.6.1 \
	nbdkit-release-notes-1.8.1 \
	nbdkit-release-notes-1.10.1 \
	nbdkit-release-notes-1.12.1 \
	nbdkit-release-notes-1.14.1 \
	nbdkit-release-notes-1.16.1 \
	nbdkit-release-notes-1.18.1 \
	nbdkit-release-notes-1.20.1 \
	nbdkit-release-notes-1.22.1 \
	nbdkit-release-notes-1.24.1 \
	nbdkit-release-notes-1.26.1 \
	nbdkit-release-notes-1.28.1 \
	nbdkit-security.1 \
	nbdkit-service.1 \
	nbdkit-tls.1 \
	nbdkit-plugin.3 \
	nbdkit-filter.3 \
	$(NULL)
CLEANFILES += $(man_MANS)

nbdkit.1: nbdkit.pod plugin-links.pod lang-plugin-links.pod filter-links.pod \
		synopsis.txt
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    --insert plugin-links.pod:__PLUGIN_LINKS__ \
	    --insert lang-plugin-links.pod:__LANG_PLUGIN_LINKS__ \
	    --insert filter-links.pod:__FILTER_LINKS__ \
	    --verbatim $(srcdir)/synopsis.txt:__SYNOPSIS__ \
	    $<

nbdkit-captive.1: nbdkit-captive.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-client.1: nbdkit-client.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-loop.1: nbdkit-loop.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-probing.1: nbdkit-probing.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-protocol.1: nbdkit-protocol.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-release-notes-1.4.1: nbdkit-release-notes-1.4.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-release-notes-1.6.1: nbdkit-release-notes-1.6.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-release-notes-1.8.1: nbdkit-release-notes-1.8.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-release-notes-1.10.1: nbdkit-release-notes-1.10.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-release-notes-1.12.1: nbdkit-release-notes-1.12.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-release-notes-1.14.1: nbdkit-release-notes-1.14.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-release-notes-1.16.1: nbdkit-release-notes-1.16.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-release-notes-1.18.1: nbdkit-release-notes-1.18.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-release-notes-1.20.1: nbdkit-release-notes-1.20.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-release-notes-1.22.1: nbdkit-release-notes-1.22.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-release-notes-1.24.1: nbdkit-release-notes-1.24.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-release-notes-1.26.1: nbdkit-release-notes-1.26.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-release-notes-1.28.1: nbdkit-release-notes-1.28.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-security.1: nbdkit-security.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-service.1: nbdkit-service.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-tls.1: nbdkit-tls.pod
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-plugin.3: nbdkit-plugin.pod plugin-links.pod lang-plugin-links.pod
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    --insert plugin-links.pod:__PLUGIN_LINKS__ \
	    --insert lang-plugin-links.pod:__LANG_PLUGIN_LINKS__ \
	    $<

nbdkit-filter.3: nbdkit-filter.pod filter-links.pod
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    --insert filter-links.pod:__FILTER_LINKS__ \
	    $<

endif HAVE_POD

# All plugins.
plugin-links.pod: $(top_srcdir)/configure.ac
	rm -f $@ $@-t
	$(srcdir)/make-links.sh plugin 1 $(non_lang_plugins) > $@-t
	echo \; >> $@-t
	$(srcdir)/make-links.sh plugin 3 $(lang_plugins) >> $@-t
	mv $@-t $@

# Only language plugins (in section 3).
lang-plugin-links.pod: $(top_srcdir)/configure.ac
	rm -f $@ $@-t
	$(srcdir)/make-links.sh plugin 3 $(lang_plugins) >> $@-t
	mv $@-t $@

# All filters.
filter-links.pod: $(top_srcdir)/configure.ac
	rm -f $@ $@-t
	$(srcdir)/make-links.sh filter 1 $(filters) > $@-t
	mv $@-t $@

DISTCLEANFILES = plugin-links.pod lang-plugin-links.pod filter-links.pod
