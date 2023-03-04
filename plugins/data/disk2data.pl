#!/usr/bin/env perl
# -*- perl -*-
# Convert disk image or file to nbdkit-data-plugin command line.
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

=pod

=head1 NAME

disk2data.pl - convert disk image or file to nbdkit-data-plugin command line

=head1 SYNOPSIS

 ./disk2data.pl DISK

=head1 EXAMPLE

=head2 Create a 1 MB MBR-partitioned disk command line

 $ rm -f disk
 $ truncate -s 1M disk
 $ echo start=1 | sfdisk disk
 $ ./disk2data.pl disk

The above command will print out the full nbdkit command to use,
similar to:

 $ nbdkit data data="
   @0x1b8 202 127 39 107 0 0 0 0 2 0 131 32 32 0 1 0 0 0 255 7
   @0x1fe 85 170
   " size=1048576

=head2 Create a 1 GB GPT-partitioned disk command line

 $ rm -f disk
 $ truncate -s 1G disk
 $ sgdisk -n 1 disk
 $ ./disk2data.pl disk

A command similar to this is printed:

 $ nbdkit data data="
   @0x1c0 2 0 238 138 8 130 1 0 0 0 255 255 31
   @0x1fe 85 170 69 70 73 32 80 65 82 84 0 0 1 0 92 0 0 0 110 92 89 223
   0 0 0 0 1 @0x220 255 255 31 @0x228 34
   @0x230 222 255 31 @0x238 159 149 54 193 83 188 210 70 172 3 15 147
   241 183 1 61 2 @0x250 128 0 0 0 128 0 0 0 193 75 104 199
   @0x400 175 61 198 15 131 132 114 71 142 121 61 105 216 71 125 228 237
   59 243 84 97 170 76 75 179 212 47 233 110 221 119 58 0 8
   @0x428 222 255 31 @0x3fffbe00 175 61 198 15 131 132 114 71 142 121 61
   105 216 71 125 228 237 59 243 84 97 170 76 75 179 212 47 233 110 221
   119 58 0 8 @0x3fffbe28 222 255 31
   @0x3ffffe00 69 70 73 32 80 65 82 84 0 0 1 0 92 0 0 0 178 45 163 71 0
   0 0 0 255 255 31 @0x3ffffe20 1
   @0x3ffffe28 34 @0x3ffffe30 222 255 31
   @0x3ffffe38 159 149 54 193 83 188 210 70 172 3 15 147 241 183 1 61
   223 255 31 @0x3ffffe50 128 0 0 0 128 0 0 0 193 75 104 199
   " size=1073741824

=head1 DESCRIPTION

C<disk2data.pl> is a simple script which converts a disk image or file
to the C<data="..."> format used by L<nbdkit-data-plugin(1)>.

Most operating systems have command line size limits which are quite a
lot smaller than any desirable disk image, so specifying a large,
fully populated disk image on the command line is not be possible.
You can only use this with small or very sparse disk images.

=head1 SEE ALSO

L<nbdkit-data-plugin(1)>,
L<nbdkit(1)>.

=head1 AUTHORS

Richard W.M. Jones

=head1 COPYRIGHT

Copyright Red Hat

=cut

use strict;

die "$0: expecting a single disk image parameter\n" unless @ARGV == 1;

my $disk = $ARGV[0];
my $size = (stat ($disk))[7];

open FH, "<:raw", $disk or die "$0: $disk: $!\n";

print "nbdkit data data=\"\n  ";
my $col = 2;

my $offset = 0;
my $c;

while ($offset < $size) {
    my $old_offset = $offset;
    my $r;

    # Find the next non-zero data.
    while ($r = read FH, $c, 1) {
        $offset++;
        last unless $c eq "\0";
    }
    die "$0: $disk: read: $!" unless defined $r;

    # End of the file?
    last if $r == 0;

    # Go back one character to unconsume the !\0.
    seek FH, -1, 1;
    $offset--;

    # Did we move forwards in that loop?  If so we must emit a new
    # offset.  But if we only moved forward a few bytes then emitting
    # zeroes is more efficient.
    my $d = $offset - $old_offset;
    if ($d <= 4) {
        for (my $i = 0; $i < $d; ++$i) {
            emit ("0");
        }
    }
    else {
        emit (sprintf ('@0x%x', $offset));
    }

    # Look for short-period repeated data.
    my $max_period = 8;
    my $old_offset = $offset;
    $r = read FH, $c, 2*$max_period;
    last if $r == 0;
    seek FH, $old_offset, 0;
    if ($r == 2*$max_period) {
        my $period;
        my $found;
        for ($period = 1; $period <= $max_period; ++$period) {
            my $pattern = substr ($c, 0, $period);
            if ($pattern eq substr ($c, $period, $period)) {
                my $repeats = 0;
                while ($r = read FH, $c, $period) {
                    if ($c ne $pattern) {
                        seek FH, $offset, 0;
                        last;
                    }
                    $offset += $period;
                    $repeats++;
                }
                die if $repeats <= 1;
                if ($period == 1) {
                    emit (sprintf ("%d*%d", ord ($pattern), $repeats));
                }
                else {
                    emit ("(");
                    for (my $i = 0; $i < $period; ++$i) {
                        emit (sprintf ("%d", ord (substr ($pattern, $i, 1))));
                    }
                    emit (sprintf (")*%d", $repeats));
                }
                $found = 1;
                last
            }
        }
        next if $found;
    }

    # Emit non-zero data.
    while ($r = read FH, $c, 1) {
        $offset++;
        last if $c eq "\0";
        emit (sprintf ("%d", ord ($c)));
    }
    die "$0: $disk: read: $!" unless defined $r;

    # End of the file?
    last if $r == 0;

    # Go back one character to unconsume the \0.
    seek FH, -1, 1;
    $offset--;
}

if ($col > 2) {
    print "\n  ";
}
# It would be possible to be smarter about when to print the size, but
# it's safest and simplest to always print it.
print "\" size=$size\n";

sub emit
{
    my $s = shift;
    my $is_offset = substr ($s, 0, 1) eq "@";
    my $n = length $s;

    # This means we prefer to start an offset on a new line if the
    # line is already over half way across.
    my $limit;
    if ($is_offset) { $limit = 40 } else { $limit = 72 }

    if ($col + $n + 1 > $limit) {
        print "\n  ";
        $col = 2;
    }

    print $s, " ";
    $col += $n + 1;
}
