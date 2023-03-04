#!@sbindir@/nbdkit perl
# -*- perl -*-

=pod

=head1 NAME

nbdkit-example4-plugin - example nbdkit plugin written in Perl

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

=head1 VERSION

C<nbdkit-example4-plugin> first appeared in nbdkit 1.2.

=head1 SEE ALSO

L<https://gitlab.com/nbdkit/nbdkit/blob/master/plugins/example4/example4.pl>,
L<nbdkit(1)>,
L<nbdkit-plugin(3)>,
L<nbdkit-perl-plugin(3)>.

=head1 AUTHORS

Richard W.M. Jones

=head1 COPYRIGHT

Copyright Red Hat

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

# After all the parameters have been parsed, this can be used to check
# for invalid or missing parameters.
sub config_complete
{
    die "size was not set" unless defined $size;
}

# This is called just before forking into the background and is the
# last opportunity for the plugin to print an error message that the
# user can see (without digging through log files).  Here we allocate
# the disk.
sub get_ready
{
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
