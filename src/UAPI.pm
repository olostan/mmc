package P;

use strict;
use vars qw(%_hooks);

use CL;
use Keymap;
use MUD;
use DCommand;

%_hooks=();

sub echo($) {
    CL::toutput(0,CL::parse_colors($_[0]));
}

sub printc(@) {
  my $line=join('',@_);
  $line =~ s/&(.)/$1 eq "&" ? "&" : "\003$1"/ge;
  CL::toutput(0,CL::parse_colors($line));
}

sub wecho($$) {
    CL::toutput($_[0],CL::parse_colors($_[1]));
}

sub wprintc($@) {
  my $w=shift;
  my $line=join('',@_);
  $line =~ s/&(.)/$1 eq "&" ? "&" : "\003$1"/ge;
  CL::toutput($w,CL::parse_colors($line));
}

sub printcl(@) {
  my $line=join('',@_);
  $line =~ s/&(.)/$1 eq "&" ? "&" : "\003$1"/ge;
  my $tmp;
  CL::toutput(0,$tmp=CL::parse_colors($line));
  MUD::clog($tmp);
}

sub wprintcl($@) {
  my $w=shift;
  my $line=join('',@_);
  $line =~ s/&(.)/$1 eq "&" ? "&" : "\003$1"/ge;
  my $tmp;
  CL::toutput($w,$tmp=CL::parse_colors($line));
  MUD::clog($tmp);
}

sub msg(@) {
  CL::msg(join($,,@_));
}

sub trig(&$;$) {
  my $c=shift;
  my $p=shift;
  my $f=shift||"";
  MUD::add_trigger($p,undef,$c,$f);
}

sub subst($$;$) {
  my ($p,$c,$f)=@_;
  $f||="";
  MUD::add_trigger($p,Parser::parse_string($c),undef,$f);
}

sub gag($;$) {
  my ($p,$f)=@_;
  $f||="";
  MUD::add_trigger($p,undef,undef,"g$f");
}

sub xsub(&$$;$) {
  my ($c,$p,$s,$f)=@_;
  $f||="";
  MUD::add_trigger($p,Parser::parse_string($s),$c,$f);
}

sub alias(&$) {
  no strict 'refs';
  *{"U::$_[1]"}=$_[0];
}

sub bindkey(&$) {
    Keymap::bindkey($_[1],$_[0]);
}

sub hook(&$) {
  $_hooks{$_[1]}=[$_[0]];
}

sub sendl($) {
  MUD::sendl($_[0]);
}

sub timeout(&$;$) {
  my $code=shift;
  my $delay=shift;
  my $count=shift;
  new DCommand([$count,$delay,$code,[0,0,1,"$code"]]);
}

sub enable(@) {
  for my $t (@_) {
    MUD::enable_trigger($t);
  }
}

sub disable(@) {
  for my $t (@_) {
    MUD::disable_trigger($t);
  }
}

sub nosave(@) {
  for my $f (@_) {
    $U::_nosave_vars{$f}=1;
  }
}

package U;
# user defined aliases and variables go here
%U::_nosave_vars=(); # variables in this list are not saved
%U::_nosave_aliases=(); # aliases in this list are not saved

1;
