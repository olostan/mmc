package Ex;
use strict;
use integer;

sub catch(&$;$) {
	local $^W=0;
	if ($_[2]) {
		splice(@{$_[2]},0,0,$_[0],$_[1]);
		return $_[2];
	}
	return [$_[0],$_[1]];
}

sub throw($;$) {
	die [@_];
}

sub try(&;$) {
	local $^W=0;
	my $code=shift;
	{ # a special trick to peek at the caller's args
	  package DB;
	  my @info=caller(1);
	}
	eval { &$code(@DB::args) };
	if ($@ && ref($_[0]) eq "ARRAY") {
		my $a=shift;
		if (ref($@) eq "ARRAY") {
			my $etype=$@->[0];
			my $einfo=$@->[1];
			for (my $i=0;$i<=$#$a;$i+=2) {
				if ($a->[$i+1] eq $etype || $a->[$i+1] eq "...") {
					&{$a->[$i]}($etype,$einfo);
					return;
				}
			}
		} else {
			for (my $i=0;$i<=$#$a;$i+=2) {
				if ($a->[$i+1] eq "...") {
					my $et="#perl";
					my $ei=$@;
					chomp($ei);
					$ei =~ tr/\000-\037/ /s;
					&{$a->[$i]}($et,$ei);
					$@=undef;
					return;
				}
			}
		}
	}
	die $@ if $@;
}

sub import {
	no strict 'refs';
	my $callpkg=caller;
	*{"${callpkg}::try"}=\&try;
	*{"${callpkg}::catch"}=\&catch;
	*{"${callpkg}::throw"}=\&throw;
}

1;
