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

source = example4.pl

EXTRA_DIST = \
	$(source) \
	$(NULL)

if HAVE_PERL

plugin_SCRIPTS = nbdkit-example4-plugin
CLEANFILES += nbdkit-example4-plugin

# We have to do the rewriting here to avoid stupid exec_prefix.
nbdkit-example4-plugin: $(source)
	rm -f $@ $@-t
	$(SED) 's,\@sbindir\@,${sbindir},g' < $< > $@-t
	mv $@-t $@
	chmod 0555 $@

if HAVE_POD

man_MANS = nbdkit-example4-plugin.1
CLEANFILES += $(man_MANS)

nbdkit-example4-plugin.1: $(source) \
		$(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=1 --name nbdkit-example4-plugin --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

endif HAVE_POD

endif
