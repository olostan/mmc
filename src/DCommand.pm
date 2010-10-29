package DCommand;

use strict;

use CL;
use base 'CL::Timer';
use fields qw(cmd);

use vars qw(%cmdlist $lastdcmd);

%cmdlist=();
$lastdcmd='';

sub init {
    no integer;
    my DCommand $self=shift;
    my $cmd=shift;

    $self->SUPER::init(CL::gettime()+$cmd->[1]/1000.0);
    $self->{cmd}=$cmd;
    $cmdlist{$self}=$self;
    $lastdcmd="$self";
    $lastdcmd =~ s/.*\(//;
    $lastdcmd =~ s/\).*//;
}

sub new {
    no strict 'refs';
    my $class=shift;
    my DCommand $self = fields::new($class);

    $self->init(@_);

    if (!$self->{cmd}[0] || $self->{cmd}[0]>1) {
	&{$self->{cmd}[2]};
	--$self->{cmd}[0];
    }

    $self;
}

sub run {
    no integer;
    my DCommand $self=shift;
    &{$self->{cmd}->[2]};
    if (!$self->{cmd}[0] || --$self->{cmd}[0]) {
	$self->SUPER::init($self->{when}+$self->{cmd}->[1]/1000.0);
    } else {
	delete $cmdlist{$self};
	if ($self->{cmd}->[4]) {
	  my $n="$self";
	  $n=~s/\).*//;
	  $n=~s/.*\(0x//;
	  CL::msg("{" . ($self->{cmd}[3] ? CMD::format_cmd($self->{cmd}[3]) : '') ."}\@$n finished");
	}
    }
}

sub remove($) {
  my $self=shift;
  $self->SUPER::cancel;
  delete $cmdlist{$self};
}

1;
