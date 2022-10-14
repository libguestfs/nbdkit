#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2017 Red Hat Inc.
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

set -e

# This creates the PKI files for the TLS tests.  However if certtool
# doesn't exist, just create an empty directory instead.

if [ -z "$SRCDIR" ] || [ ! -f "$SRCDIR/test-tls.sh" ]; then
    echo "$0: script is being run from the wrong directory."
    echo "Don't try to run this script by hand."
    exit 1
fi

rm -rf pki pki-t

mkdir pki-t

if ! certtool --help >/dev/null 2>&1; then
    echo "$0: certtool not found, TLS tests will be skipped."
    touch pki-t/.stamp
    mv pki-t pki
    exit 0
fi

# Create the CA.
certtool --generate-privkey > pki-t/ca-key.pem
chmod 0600 pki-t/ca-key.pem

cat > pki-t/ca.info <<EOF
cn = Test
ca
cert_signing_key
EOF
certtool --generate-self-signed \
         --load-privkey pki-t/ca-key.pem \
         --template pki-t/ca.info \
         --outfile pki-t/ca-cert.pem

# Create the server certificate and key.
certtool --generate-privkey > pki-t/server-key.pem
chmod 0600 pki-t/server-key.pem

cat > pki-t/server.info <<EOF
organization = Test
cn = localhost
dns_name = localhost
ip_address = 127.0.0.1
ip_address = ::1
tls_www_server
encryption_key
signing_key
EOF
certtool --generate-certificate \
         --load-ca-certificate pki-t/ca-cert.pem \
         --load-ca-privkey pki-t/ca-key.pem \
         --load-privkey pki-t/server-key.pem \
         --template pki-t/server.info \
         --outfile pki-t/server-cert.pem

# Create a client certificate and key.
certtool --generate-privkey > pki-t/client-key.pem
chmod 0600 pki-t/client-key.pem

cat > pki-t/client.info <<EOF
country = US
state = New York
locality = New York
organization = Test
cn = localhost
tls_www_client
encryption_key
signing_key
EOF
certtool --generate-certificate \
         --load-ca-certificate pki-t/ca-cert.pem \
         --load-ca-privkey pki-t/ca-key.pem \
         --load-privkey pki-t/client-key.pem \
         --template pki-t/client.info \
         --outfile pki-t/client-cert.pem

# Finish off.
touch pki-t/.stamp
mv pki-t pki
