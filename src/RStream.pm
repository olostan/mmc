package RStream;
use strict;
use integer;

use CL; # force registering CL::Timer and CL::Socket

sub TIEHANDLE {
    my $class=shift;
    my $self={ printer => \&CL::msg , buffer => '' };
    $self->{printer}=\&CL::err if ($_[0]);
    bless $self,$class;
}

sub PRINT {
    my $self=shift;
    for my $s (@_) {
	my @sl=split(/\n/,$s,-1);
	$sl[0]=$self->{buffer} . $sl[0];
	$self->{buffer}=pop(@sl);
	for my $l (@sl) { $l =~ tr/\000-\037/ /s; &{$self->{printer}}($l) }
    }
}

sub DESTROY {
    my $self=shift;
    &{$self->{printer}}($self->{buffer}) if $self->{buffer};
}

BEGIN { # stop perl from bitching to stdout
  if (!$::moddep_run) {
    $SIG{__WARN__}=sub {
      my $em=$_[0]||"";
      return if $em =~ /^Unquoted string/; # ignore these warnings deliberately
      chomp($em);
      for (split(/\n/,$em)) {
	tr/\000-\037/ /s;
	CL::warn("#perl: $_");
      }
    };
    $SIG{__DIE__}=sub {
      my $em=$_[0]||"";
      chomp($em);
      for (split(/\n/,$em)) {
	tr/\000-\037/ /s;
	CL::err("#perl: $_");
      }
    }
  }
}

1;
