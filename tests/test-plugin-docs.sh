#!/usr/bin/env bash
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

# Test the nbdkit <plugin> --help output matches the associated POD
# documentation.

source ./functions.sh
set -e

# There's no point doing this test under valgrind.
skip_if_valgrind

rm -f plugin-keys.txt pod-keys.txt
cleanup_fn rm -f plugin-keys.txt pod-keys.txt

errors=0

run_test ()
{
    plugin="$1"

    pod=../plugins/$plugin/nbdkit-$plugin-plugin.pod
    test -f "$pod"

    # Get the key=value lines from the help output.
    nbdkit $plugin --help |
        grep -Eo '^[-_A-Za-z0-9]+=|^\[[-_A-Za-z0-9]+=\]' |
        $SED -e 's/\[//' -e 's/\]//' > plugin-keys.txt

    # Get the key=value lines from the POD documentation.
    cat "$pod" |
        grep -Eo '^=item B<[-_A-Za-z0-9]+=|^=item \[B<[-_A-Za-z0-9]+=' |
        $SED -e 's/=item //' -e 's/B<//' -e 's/\[//' > pod-keys.txt

    # Check each plugin --help key is mentioned in the POD
    # documentation.
    for key in `cat plugin-keys.txt`; do
        if ! grep -sq "^$key" pod-keys.txt; then
            echo "error: $pod: documentation does not mention '$key...'" >&2
            ((errors++)) ||:
        fi
    done

    # Check each POD documentation key is mentioned in plugin --help.
    for key in `cat pod-keys.txt`; do
        if ! grep -sq "^$key" plugin-keys.txt; then
            echo -n "error: nbdkit-$plugin-plugin: " >&2
            echo "--help output does not mention '$key...'" >&2
            ((errors++)) ||:
        fi
    done
}

do_test ()
{
    case "$1" in
        cc|eval|golang|lua|ocaml|perl|python|ruby|rust|sh|tcl)
            # Skip all language plugins as this test doesn't
            # make sense for these.
            ;;
        S3|example4)
            # The --help output is for the language plugin (like
            # perl or python) not the actual plugin.  This is
            # really a bug in how we do --help output, for now
            # ignore.
            ;;
        blkio)
            # The --help output describes the generic PROPERTY= which
            # breaks the test.  Ignore it.
            ;;
        example2|example3)
            # The example documentation doesn't list options.
            # Should it?  Possibly, but ignore for now.
            ;;
	nbd)
	    # Because of macOS SIP misfeature the DYLD_* environment
	    # variable added by libnbd/run is filtered out and the
	    # test won't work.  Skip it entirely on Macs.
	    if test "$(uname)" != "Darwin"; then run_test $1; fi
	    ;;
        vddk)
            # This plugin has many parameters and the --help output
            # directs you to the manual.  Ignore it.
            ;;
        *)
            run_test $1
            ;;
    esac
}
foreach_plugin do_test

if [ "$errors" -ge 1 ]; then exit 1; fi
