use strict;

my $disk = "\0" x (1024*1024);

BEGIN {
    # Call the debug function to check it works.
    Nbdkit::debug ("hello world!");

    # Check some expected constants are defined.  Since these constants
    # are defined by the nbdkit ABI, they should never change so checking
    # their absolute values here ought to be fine.
    die unless $Nbdkit::FLAG_MAY_TRIM == 1;
    die unless $Nbdkit::FLAG_FUA      == 2;
    die unless $Nbdkit::FLAG_REQ_ONE  == 4;
    die unless $Nbdkit::FUA_NATIVE    == 2;
    die unless $Nbdkit::CACHE_EMULATE == 1;
    die unless $Nbdkit::EXTENT_ZERO   == 2;
}

sub config_complete
{
}

sub open
{
    my $readonly = shift;
    Nbdkit::debug ("perl plugin opened, readonly=" . $readonly);
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

sub can_write
{
    my $h = shift;
    return 1;
}

sub can_flush
{
    my $h = shift;
    return 1;
}

sub is_rotational
{
    my $h = shift;
    return 0;
}

sub can_trim
{
    my $h = shift;
    return 1;
}

sub pread
{
    my $h = shift;
    my $count = shift;
    my $offset = shift;
    return substr ($disk, $offset, $count);
}

sub pwrite
{
    my $h = shift;
    my $buf = shift;
    my $count = length ($buf);
    my $offset = shift;
    substr ($disk, $offset, $count) = $buf;
}

sub flush
{
    my $h = shift;
}

sub trim
{
    my $h = shift;
    my $count = shift;
    my $offset = shift;
}
