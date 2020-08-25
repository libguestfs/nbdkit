#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2019-2020 Red Hat Inc.
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

source ./functions.sh
set -e
set -x

requires_plugin sh
requires nbdsh -c 'print(h.set_full_info)'

export sock=`mktemp -u`
files="$sock export-info.pid"
rm -f $files
cleanup_fn rm -f $files

# Create an nbdkit sh plugin for checking NBD_INFO replies to NBD_OPT_GO.
# XXX Update when .default_export and .export_description are implemented in sh
start_nbdkit -P export-info.pid -U $sock \
             sh - <<'EOF'
case "$1" in
    default_export) echo hello ;;
    open) echo "$3" ;;
    export_description) echo "$2 world" ;;
    get_size) echo 0 ;;
    *) exit 2 ;;
esac
EOF

# Without client request, nothing is advertised
nbdsh -c '
import os

h.set_opt_mode(True)
h.connect_unix(os.environ["sock"])

h.set_export_name("a")
h.opt_info()
try:
  h.get_canonical_export_name()
  assert False
except nbd.Error as ex:
  pass

h.set_export_name("")
h.opt_info()
try:
  h.get_canonical_export_name()
  assert False
except nbd.Error as ex:
  pass

h.opt_go()
try:
  h.get_canonical_export_name()
  assert False
except nbd.Error as ex:
  pass

h.shutdown()
'

# With client request, reflect the export name back
nbdsh -c '
import os

h.set_opt_mode(True)
h.set_full_info(True)
h.connect_unix(os.environ["sock"])

h.set_export_name("a")
h.opt_info()
assert h.get_canonical_export_name() == "a"

h.set_export_name("")
h.opt_info()
# XXX Adjust once default export works
assert h.get_canonical_export_name() == ""

h.opt_go()
assert h.get_canonical_export_name() == ""

h.shutdown()
'
