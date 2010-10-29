package Ticker;

use CL;
use Ex;

use base qw(CL::Timer);
use fields qw(interval state);

use vars qw($tc);

sub new {
  no strict 'refs';
  my $class=shift;
  my Ticker $self = fields::new($class);
  $self->{interval}=shift;
  $self->{state}=0;
  $self;
}

sub setsize($$) {
  my Ticker $self=shift;
  my $int=shift;
  $self->{interval}=$int;
  $self->restart if ($self->{state}>0);
}

sub restart($) {
  my Ticker $self=shift;
  $self->{state}=1;
  $self->init(CL::gettime()+$self->{interval}-10);
}

sub stop($) {
  my Ticker $self=shift;
  $self->{state}=0;
  $self->cancel;
}

sub run($) {
  my Ticker $self=shift;
  if ($self->{state}==1) {
    $self->init($self->{when}+10);
    CL::msg("TICK in 10 seconds");
    ::call_hook('pretick');
    $self->{state}=2;
  } elsif ($self->{state}==2) {
    $self->init($self->{when}+$self->{interval}-10);
    CL::msg("TICK");
    ::call_hook('tick');
    $self->{state}=1;
  }
}

sub getstr() {
  if ($tc->{state}==2) {
    return int($tc->{when}-CL::gettime());
  } elsif ($tc->{state}==1) {
    return int($tc->{when}+10-CL::gettime());
  }
  return '--';
}

sub info($) {
  my Ticker $self=shift;
  if ($self->{state}==2) {
    CL::msg(sprintf("%d",$self->{when}-CL::gettime()) . " seconds till tick.");
  } elsif ($self->{state}==1) {
    CL::msg(sprintf("%d",$self->{when}+10-CL::gettime()) . " seconds till tick.");
  } else {
    CL::msg("Ticker is not running.");
  }
}

$tc=new Ticker(62);

package Ticker2;

use base 'CL::Timer';
use fields qw(ns);

sub new {
  no strict 'refs';
  my $class=shift;
  my Ticker2 $self = fields::new($class);
  $self->{ns}=CL::gettime()+1;
  $self->init($self->{ns});
  $self;
}

my $last='';

sub run {
  my Ticker2 $self=shift;
  my $cur=Ticker::getstr();
  if ($cur ne $last) {
    $U::_ticker=$cur;
    $last=$cur;
  }
  $self->{ns}+=1.0;
  $self->init($self->{ns});
}

$U::_nosave_vars{'_ticker'}=1;

1;
