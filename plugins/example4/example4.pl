#!@sbindir@/nbdkit perl
# -*- perl -*-

=pod

=encoding utf8

=head1 NAME

nbdkit-example4-plugin - An example nbdkit plugin written in Perl

=head1 SYNOPSIS

 nbdkit example4 size=<N>

=head1 EXAMPLE

 nbdkit example4 size=1048576
 guestfish -a nbd://localhost

=head1 DESCRIPTION

C<nbdkit-example4-plugin> is an example L<nbdkit(1)> plugin
written in Perl.

The C<size=N> parameter is required.  It specifies the disk size in
bytes.  A single disk image, initially all zeroes, is created and can
be read and written by all clients.  When nbdkit shuts down the disk
image is thrown away.

Mainly this is useful for testing and as an example of nbdkit plugins
written in Perl.  You should also read L<nbdkit-plugin(3)> and
L<nbdkit-perl-plugin(3)>.  Note that nbdkit plugins may be written in
C or other scripting languages.

=head1 SEE ALSO

L<example4.pl> in the nbdkit source tree,
L<nbdkit(1)>,
L<nbdkit-plugin(3)>,
L<nbdkit-perl-plugin(3)>.

=head1 AUTHORS

Richard W.M. Jones

=head1 COPYRIGHT

Copyright (C) 2017 Red Hat Inc.

=head1 LICENSE

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

=over 4

=item *

Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

=item *

Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

=item *

Neither the name of Red Hat nor the names of its contributors may be
used to endorse or promote products derived from this software without
specific prior written permission.

=back

THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.

=cut

use strict;

my $disk;
my $size;

# size=<N> (in bytes) is required on the command line.
# This example could be improved by parsing strings such as "1M".
sub config
{
    my $k = shift;
    my $v = shift;

    if ($k eq "size") {
        $size = int ($v);
    }
    else {
        die "unknown parameter $k";
    }
}

# When all config parameters have been seen, allocate the disk.
sub config_complete
{
    die "size was not set" unless defined $size;
    $disk = "\0" x $size;
}

# Accept a connection from a client, create and return the handle
# which is passed back to other calls.
sub open
{
    my $readonly = shift;
    my $h = { readonly => $readonly };
    return $h;
}

# Close the connection.
sub close
{
    my $h = shift;
}

# Return the size.
sub get_size
{
    my $h = shift;
    return length ($disk);
}

# Read.
sub pread
{
    my $h = shift;
    my $count = shift;
    my $offset = shift;
    return substr ($disk, $offset, $count);
}

# Write.
sub pwrite
{
    my $h = shift;
    my $buf = shift;
    my $count = length ($buf);
    my $offset = shift;
    substr ($disk, $offset, $count) = $buf;
}

# If you want to display extra information about the plugin when
# the user does ‘nbdkit example4 --dump-plugin’ then you can print
# ‘key=value’ lines here.
sub dump_plugin
{
    print "example4_extra=hello\n";
    flush STDOUT;
}
