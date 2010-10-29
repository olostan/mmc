package CL;

BEGIN {
  $INC{"CL/Socket.pm"}="?";
  $INC{"CL/Timer.pm"}="?"; # hack?
  $CL::winfo=0;
}

sub msg($) {
  MUD::clog(CL::parse_colors("-:- $_[0]"));
  CL::i_msg($_[0]);
}

sub clip() {
  my $clip=CL::get_clipboard();
  $clip =~ tr/\r//d if defined $clip;
  $clip;
}

package CL::Socket;

use strict;
use integer;

use fields qw(rh rp lh dp _fd);

use Ex;

sub init {
    my CL::Socket $self=shift;
    $self->{rh}=shift;
    $self->{rp}=shift;
    $self->{dp}=shift;
    $self->{lh}=shift||'';
    $self->{_fd}=-1;

    $self->{dp}=1 if !defined($self->{dp});
    if ($self->{lh}) {
      $self->{_fd}=CL::sconnect($self->{rh},$self->{rp},$self,$self->{dp},$self->{lh});
    } else {
      $self->{_fd}=CL::sconnect($self->{rh},$self->{rp},$self,$self->{dp});
    }
    throw("SocketError") if ($self->{_fd}<0);
    $self;
}

sub init_pipe {
  my CL::Socket $self=shift;
  $self->{rh}=$self->{rp}=$self->{lh}='';
  my $cmd=shift;
  $self->{dp}=shift;
  $self->{_fd}=CL::sconnect_pipe($cmd,$self,$self->{dp});
  throw("SocketError") if ($self->{_fd}<0);
  $self;
}

sub new {
    no strict 'refs';
    my $class=shift;
    my CL::Socket $self = fields::new($class);
    $self->init(@_);
}

sub new_pipe {
  no strict 'refs';
  my $class=shift;
  my CL::Socket $self = fields::new($class);
  $self->init_pipe(@_);
}

sub connected($$) { }
sub remclosed($) { }
sub closed($) { }
sub echo($$) { }
sub line($$) { }
sub prompt($$) { }
sub bell($) { }

sub write {
    my CL::Socket $self=$_[0];
    CL::swrite($self->{_fd},$_[1]);
}

sub writeln {
    my CL::Socket $self=$_[0];
    CL::swrite($self->{_fd},$_[1]);
    CL::swriteln($self->{_fd});
}

sub close {
    my CL::Socket $self=$_[0];
    CL::sclose($self->{_fd});
    $self->{_fd}=-1;
}

sub info {
    my CL::Socket $self=$_[0];
    CL::sgetcounters($self->{_fd});
}

sub tcp_info {
    my CL::Socket $self=$_[0];
    CL::get_tcp_info($self->{_fd},$_[1]);
}

sub set_lp {
  my CL::Socket $self=$_[0];
  CL::sock_setlp($self->{_fd},$_[1]);
}

package CL::Timer;

use strict;
use integer;

use fields qw(_id when);

use Ex;

sub new {
    no strict 'refs';
    my $class=shift;
    my CL::Timer $self = fields::new($class);

    $self->{_id}=-1;
    $self->init(@_);
    $self;
}

sub init($$) {
    my CL::Timer $self=shift;
    $self->{when}=shift;
    $self->cancel() if (defined($self->{_id}));
    $self->{_id}=CL::timeout($self->{when},$self);
    throw("TimerError") if ($self->{_id}<0);
}

sub init2($) {
    my CL::Timer $self=shift;
    $self->{_id}=-1;
}

sub _run($) { my CL::Timer $self=$_[0]; $self->{_id}=-1; $self->run; }
sub run($) { }

sub cancel {
    my CL::Timer $self=shift;
    if (defined($self->{_id}) && $self->{_id}>=0) {
	CL::cancel_timeout($self->{_id});
	$self->{_id}=-1;
    }
}

1;
