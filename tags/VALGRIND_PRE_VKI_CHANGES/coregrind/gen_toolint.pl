#!/usr/bin/perl

#  This file is part of Valgrind, an extensible x86 protected-mode
#  emulator for monitoring program execution on x86-Unixes.
#
#  Copyright (C) 2000-2004 Julian Seward 
#     jseward@acm.org
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License as
#  published by the Free Software Foundation; either version 2 of the
#  License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
#  02111-1307, USA.
#
#  The GNU General Public License is contained in the file COPYING.

use strict;
use warnings;

my $output = shift @ARGV;
my $indent = "";
my $headerguard;
my $include;
my $passcomment = 1;
my $pre;
my $post;
my $generate;

my $struct = "VG_(tool_interface)";

my %pfxmap = ("track" => "SK_",
	      "tool"  => "SK_",
	      "malloc"=> "SK_",
	     );

sub getargnames(@) {
    my @args = @_;
    my @ret;

    foreach my $a (@args) {
	my @pieces = split /\s+/, $a;
	my $name = pop @pieces;
	push @ret, $name unless $name eq "void";
    }
    return @ret;
}

sub getargtypes(@) {
    my @args = @_;
    my @ret;

    foreach my $a (@args) {
	my @pieces = split /\s+/, $a;
	pop @pieces;
	push @ret, (join " ", @pieces);
    }
    @ret = "void" if ($#ret == -1);
    return @ret;
}

# Different output modes
if ($output eq "callwrap") {
    $include = "core.h";
    $generate = sub ($$$@) {
	my ($pfx, $ret, $func, @args) = @_;
	my $args = join ", ", @args;
	my $argnames = join ", ", getargnames(@args);
	print "$ret $pfxmap{$pfx}($func)($args)\n{\n";
	print "   return (*$struct.${pfx}_$func)($argnames);\n";
	print "}\n";
    }
} elsif ($output eq "proto") {
    $include = "core.h";
    $generate = sub ($$$@) {
	my ($pfx, $ret, $func, @args) = @_;
	my $args = join ', ', @args;

	print "$ret $pfxmap{$pfx}($func)($args);\n";
	print "Bool VG_(defined_$func)(void);\n";
    }
} elsif ($output eq "toolproto") {
    $generate = sub ($$$@) {
	my ($pfx, $ret, $func, @args) = @_;
	my $args = join ', ', @args;

	print "$ret $pfxmap{$pfx}($func)($args);\n";
    }
} elsif ($output eq "missingfuncs") {
    $include = "core.h";
    $generate = sub ($$$@) {
	my ($pfx, $ret, $func, @args) = @_;
	my $args = join ", ", @args;

	print "static $ret missing_${pfx}_$func($args) {\n";
	print "   VG_(missing_tool_func)(\"${pfx}_$func\");\n";
	print "}\n";
	print "Bool VG_(defined_$func)(void) {\n";
	print "   return $struct.${pfx}_$func != missing_${pfx}_$func;\n";
	print "}\n\n";
    };
    $indent = "   ";
} elsif ($output eq "struct") {
    $include = "core.h";
    $pre = sub () {
	print "typedef struct {\n";
    };
    $post = sub () {
	print "} VgToolInterface;\n\n";
	print "extern VgToolInterface $struct;\n"
    };
    $generate = sub ($$$@) {
	my ($pfx, $ret, $func, @args) = @_;
	my $args = join ", ", @args;

	print "$indent$ret (*${pfx}_$func)($args);\n";
    };
    $indent = "   ";
    $headerguard=$output;
} elsif ($output eq "structdef") {
    $include = "vg_toolint.h";
    $pre = sub () {
	print "VgToolInterface $struct = {\n";
    };
    $post = sub () {
	print "};\n";
    };
    $generate = sub ($$$@) {
	my ($pfx, $ret, $func, @args) = @_;

	print "$indent.${pfx}_$func = missing_${pfx}_$func,\n"
    };
    $indent = "   ";
} elsif ($output eq "initfunc") {
    $include = "tool.h";
    $generate = sub ($$$@) {
	my ($pfx, $ret, $func, @args) = @_;
	my $args = join ", ", @args;
	my $argnames = join ", ", getargnames(@args);

	print <<EOF;
void VG_(init_$func)($ret (*func)($args))
{
	if (func == NULL)
		func = missing_${pfx}_$func;
	if (VG_(defined_$func)())
		VG_(printf)("Warning tool is redefining $func\\n");
	if (func == SK_($func))
		VG_(printf)("Warning tool is defining $func recursively\\n");
	$struct.${pfx}_$func = func;
}
EOF
    }
} elsif ($output eq "initproto") {
    $generate = sub ($$$@) {
	my ($pfx, $ret, $func, @args) = @_;
	my $args = join ', ', @args;
	print "void VG_(init_$func)($ret (*func)($args));\n";
    };
    $headerguard=$output;
} elsif ($output eq "initdlsym") {
    $pre = sub () {
	print <<EOF;
#include <dlfcn.h>
void VG_(tool_init_dlsym)(void *dlhandle)
{
   void *ret;

EOF
    };
    $post = sub () {
	print "}\n";
    };
    $generate = sub ($$$@) {
	my ($pfx, $ret, $func, @args) = @_;
	my $args = join ", ", getargtypes(@args);

	print <<EOF;
   ret = dlsym(dlhandle, "vgSkin_$func");
   if (ret != NULL)
      VG_(init_$func)(($ret (*)($args))ret);

EOF
    };

    $passcomment = 0;
}

die "Unknown output format \"$output\"" unless defined $generate;

print "/* Generated by \"gen_toolint.pl $output\" */\n";

print <<EOF if defined $headerguard;

#ifndef VG_toolint_$headerguard
#define VG_toolint_$headerguard

EOF

print <<EOF if defined $include;
#include \"$include\"
EOF

&$pre() if defined $pre;	# preamble

my $state = "idle";

my $buf;
my $lines;
my $prefix;

while(<STDIN>) {
    # skip simple comments
    next if (/^#[^#]/);

    if (/^:/) {
	s/^://;
	chomp;
	$prefix=$_;
	next;
    }

    # look for inserted comments
    if (/^##/) {
	if ($state eq "idle") {
	    $state = "comment";
	    $lines = 1;
	    $_ =~ s,^## ,/* ,;
	    $buf = $_;
	    next;
	} elsif ($state eq "comment") {
	    $lines++;
	    $_ =~ s,^## ,   ,;
	    print $indent.$buf if $passcomment;
	    $buf = $_;
	    next;
	}
	next;
    }

    # blank lines in a comment are part of the comment
    if (/^\s*$/) {
	if ($state eq "comment") {
	    $lines++;
	    print $indent.$buf if $passcomment;
	    $buf = "\n";
	} else {
	    print "\n" if $passcomment;
	}
	next;
    }

    # coming out of a comment
    if ($state eq "comment") {
	chomp $buf;

	if ($passcomment) {
	    if ($lines == 1) {
		print "$indent$buf */\n";
	    } else {
		print "$indent$buf\n$indent */\n";
	    }
	}
	$buf = "";
	$state = "idle";
    }

    chomp;
    my @func = split /,\s*/;

    my $rettype = shift @func;
    my $funcname = shift @func;

    @func = "void" if scalar @func == 0;

    &$generate ($prefix, $rettype, $funcname, @func);
}

&$post() if defined $post;	# postamble

print <<EOF if defined $headerguard;

#endif /* VG_toolint_$headerguard */
EOF
