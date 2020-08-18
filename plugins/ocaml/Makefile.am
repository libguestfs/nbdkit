# nbdkit
# Copyright (C) 2014 Red Hat Inc.
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
	NBDKit.mli \
	NBDKit.ml \
	nbdkit-ocaml-plugin.pod \
	example.ml \
	$(NULL)

if HAVE_OCAML
# Requires some work to port to Windows.  Disable it for now.
if !IS_WINDOWS

ocamllibdir = $(OCAMLLIB)
ocamllib_DATA = NBDKit.mli NBDKit.cmi NBDKit.cmx NBDKit.o

NBDKit.cmi: NBDKit.mli
	$(OCAMLC) -c $< -o $@

NBDKit.cmx: NBDKit.ml NBDKit.cmi
	$(OCAMLOPT) $(OCAMLOPTFLAGS) -c $< -o $@
NBDKit.o: NBDKit.cmx

# NB: libnbdkitocaml is not a plugin.  It's a library that you have to
# link in to the OCaml plugins that you write.  nbdkit-ocaml-plugin(3)
# for more details.

lib_LTLIBRARIES = libnbdkitocaml.la

libnbdkitocaml_la_SOURCES = \
	ocaml.c \
	$(top_srcdir)/include/nbdkit-plugin.h \
	$(NULL)

libnbdkitocaml_la_CPPFLAGS = \
	-I$(OCAMLLIB) \
	-I$(top_srcdir)/include \
	$(NULL)

libnbdkitocaml_la_CFLAGS = \
	$(WARNINGS_CFLAGS) \
	-fPIC \
	$(NULL)

# The example plugin.
noinst_SCRIPTS = nbdkit-ocamlexample-plugin.so
nbdkit-ocamlexample-plugin.so: example.cmx libnbdkitocaml.la NBDKit.cmi NBDKit.cmx
	$(OCAMLOPT) $(OCAMLOPTFLAGS) \
	  -output-obj -runtime-variant _pic -o $@ \
	  NBDKit.cmx $< \
	  -cclib -L.libs -cclib -lnbdkitocaml
example.cmx: example.ml NBDKit.cmi NBDKit.cmx
	$(OCAMLOPT) $(OCAMLOPTFLAGS) -c $< -o $@

if HAVE_POD

man_MANS = nbdkit-ocaml-plugin.3
CLEANFILES += $(man_MANS)

nbdkit-ocaml-plugin.3: nbdkit-ocaml-plugin.pod
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

endif HAVE_POD
endif !IS_WINDOWS
endif HAVE_OCAML
