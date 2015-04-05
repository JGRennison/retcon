#!/usr/bin/perl

#  retcon
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version. See: COPYING-GPL.txt
#
#  This program  is distributed in the  hope that it will  be useful, but
#  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
#  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
#  General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program. If not, see <http:#www.gnu.org/licenses/>.
#
#  2015 - Jonathan G Rennison <j.g.rennison@gmail.com>
#==========================================================================

use strict;
use warnings;

use File::Slurp;
use File::Basename;
use File::Spec;

my $thisfile = basename(__FILE__);

my $infile = File::Spec->catfile(dirname(__FILE__), "twemoji", "twemoji-regex.txt");
my $outfile = File::Spec->catfile(dirname(__FILE__), "emoji-list.cpp");
my @img_dir_config = (
	{ parts => ["res", "twemoji", "16x16"], name => 16 },
	{ parts => ["res", "twemoji", "36x36"], name => 36 },
);
my $img_dir = File::Spec->catdir(dirname(__FILE__), "..", @{$img_dir_config[0]->{parts}});

my $intext = read_file($infile, binmode => ':utf8');
chomp $intext;

$intext =~ s/\\u(d[89ab][0-9a-f]{2})\\u(d[cdef][0-9a-f]{2})/handle_surrogate($1, $2)/egi;

sub handle_surrogate {
	my ($upper, $lower) = @_;
	my $char = (hex($upper) << 10) + hex($lower) - 0x35FDC00;
	return sprintf('\\\\x{%x}', $char);
}

$intext =~ s/\\u([0-9a-f]{4})/\\\\x{$1}/gi;

my @map;
my @imports;
opendir(my $dh, $img_dir) || die;
while(readdir $dh) {
	next unless /(.+)\.png$/;

	my $file = $1;
	my $sanitised_file = $1 =~ s/-/_/gr;

	my @ptr_output;

	for my $config (@img_dir_config) {
		my @dir_parts = @{$config->{parts}};
		my $name = $config->{name};
		push @imports, "extern \"C\" const unsigned char emoji_${name}_${sanitised_file}_start[] asm(\"_binary_src_" . join('_', @dir_parts) . "_${sanitised_file}_png_start\");";
		push @imports, "extern \"C\" const unsigned char emoji_${name}_${sanitised_file}_end[] asm(\"_binary_src_" . join('_', @dir_parts) . "_${sanitised_file}_png_end\");";
		push @ptr_output, "{ emoji_${name}_${sanitised_file}_start, emoji_${name}_${sanitised_file}_end }";
	}

	if($file =~ /^([[:xdigit:]]+)-([[:xdigit:]]+)$/) {
		push @map, {
			first => hex($1),
			second => hex($2),
			string => "{ 0x$1, 0x$2, " . join(", ", @ptr_output) . " }",
		};
	}
	else {
		push @map, {
			first => hex($file),
			second => 0,
			string => "{ 0x$file, 0, " . join(", ", @ptr_output) . " }",
		};
	}
}
closedir $dh;

@map = sort { $a->{first} <=> $b->{first} || $a->{second} <=> $b->{second} } @map;

my $outtext = <<"EOL";
// This file is auto-generated from $thisfile

#include "emoji-list.h"

const std::string emoji_regex = "$intext";

@{[ join("\n", @imports) ]}

const emoji_item emoji_map[] {
	@{[ join(",\n\t", map { $_->{string} } @map) ]}
};

const size_t emoji_map_size = sizeof(emoji_map) / sizeof(emoji_item);
EOL

write_file($outfile, {binmode => ':utf8'}, $outtext);
