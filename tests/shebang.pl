#!../nbdkit perl

use strict;

my $disk = "\0" x (1024*1024);

sub open
{
    my $readonly = shift;
    my $h = { readonly => $readonly };
    return $h;
}

sub close
{
    my $h = shift;
}

sub get_size
{
    my $h = shift;
    return length ($disk);
}

sub pread
{
    my $h = shift;
    my $count = shift;
    my $offset = shift;
    return substr ($disk, $offset, $count);
}
