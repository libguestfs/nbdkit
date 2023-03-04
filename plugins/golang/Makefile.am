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

plugin_sources = \
	src/libguestfs.org/nbdkit/go.mod \
	src/libguestfs.org/nbdkit/nbdkit.go \
	src/libguestfs.org/nbdkit/utils.go \
	src/libguestfs.org/nbdkit/wrappers.go \
	src/libguestfs.org/nbdkit/wrappers.h \
	$(NULL)

EXTRA_DIST = \
	$(plugin_sources) \
	config-test.go \
	dump-plugin-examples.sh \
	examples/disk/disk.go \
	examples/disk/go.mod \
	examples/dump-plugin/dumpplugin.go \
	examples/dump-plugin/go.mod \
	examples/minimal/minimal.go \
	examples/minimal/go.mod \
	examples/ramdisk/ramdisk.go \
	examples/ramdisk/go.mod \
	nbdkit-golang-plugin.pod \
	$(NULL)

if HAVE_GOLANG

# We don't have to build the nbdkit package as a separate step.  It is
# compiled and statically linked when we build any of the examples or
# build the tests.

# Examples.
noinst_DATA = \
	examples/disk/nbdkit-godisk-plugin.so \
	examples/dump-plugin/nbdkit-godump-plugin.so \
	examples/minimal/nbdkit-gominimal-plugin.so \
	examples/ramdisk/nbdkit-goramdisk-plugin.so \
	$(NULL)

examples/disk/nbdkit-godisk-plugin.so: \
	    $(plugin_sources) examples/disk/disk.go
	cd $(srcdir)/examples/disk && \
	PKG_CONFIG_PATH="$(abs_top_builddir)/server/local$${PKG_CONFIG_PATH:+:$$PKG_CONFIG_PATH}" \
	$(GOLANG) build -o $(abs_builddir)/examples/disk/nbdkit-godisk-plugin.so -buildmode=c-shared

examples/dump-plugin/nbdkit-godump-plugin.so: \
	    $(plugin_sources) examples/dump-plugin/dumpplugin.go
	cd $(srcdir)/examples/dump-plugin && \
	PKG_CONFIG_PATH="$(abs_top_builddir)/server/local$${PKG_CONFIG_PATH:+:$$PKG_CONFIG_PATH}" \
	$(GOLANG) build -o $(abs_builddir)/examples/dump-plugin/nbdkit-godump-plugin.so -buildmode=c-shared

examples/minimal/nbdkit-gominimal-plugin.so: \
	    $(plugin_sources) examples/minimal/minimal.go
	cd $(srcdir)/examples/minimal && \
	PKG_CONFIG_PATH="$(abs_top_builddir)/server/local$${PKG_CONFIG_PATH:+:$$PKG_CONFIG_PATH}" \
	$(GOLANG) build -o $(abs_builddir)/examples/minimal/nbdkit-gominimal-plugin.so -buildmode=c-shared

examples/ramdisk/nbdkit-goramdisk-plugin.so: \
	    $(plugin_sources) examples/ramdisk/ramdisk.go
	cd $(srcdir)/examples/ramdisk && \
	PKG_CONFIG_PATH="$(abs_top_builddir)/server/local$${PKG_CONFIG_PATH:+:$$PKG_CONFIG_PATH}" \
	$(GOLANG) build -o $(abs_builddir)/examples/ramdisk/nbdkit-goramdisk-plugin.so -buildmode=c-shared

# Tests.
TESTS = dump-plugin-examples.sh

CLEANFILES += \
	examples/*/nbdkit-*-plugin.h \
	examples/*/nbdkit-*-plugin.so \
	$(NULL)

if HAVE_POD

man_MANS = nbdkit-golang-plugin.3
CLEANFILES += $(man_MANS)

nbdkit-golang-plugin.3: nbdkit-golang-plugin.pod \
		$(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

endif HAVE_POD

endif HAVE_GOLANG
