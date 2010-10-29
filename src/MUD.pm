package MUD;
# A few notes about the implementation
# 1. text from mud and prompts
#    - duplicate prompts are silently discarded, however no important
#      information is lost.
#    - consecutive empty lines are always compacted to one empty line
#
# 2. internal trigger format
#    each trigger is stored in an array with the following fields:
#    0:  pattern, a compiled regexp (qr//)
#    1:  enabled/disabled flag
#    2:  multiple flag, pattern can match multiple times in a string, this is
#        used to implement highlights. This can be slow.
#    3:  replacement string, this is used in highlights and substs, this
#        is stored with colors
#    4:  perl code reference or a client command, used in plain actions
#    5:  priority
#    6:  list of trigger tags
#    7:  fall-through flag, a matching trigger doesnt termninate the search
#        when this field is true
#    8:  gag flag
#    9:  number of times the trigger mathced since enabled last time
#   10:  save flag
#   11:  original pattern
#   12:  max number of matches allowed
#
use strict;
use integer;
use locale;

use vars qw(%tags);

use Ex;
use CL;
use LE;
use Conf;
use Parser;
use Symbol qw(gensym);

use base 'CL::Socket';

my $logfile;
my $logfilename;

my @colormap=("\033[0;30m",
	      "\033[0;31m",
	      "\033[0;32m",
	      "\033[0;33m",
	      "\033[0;34m",
	      "\033[0;35m",
	      "\033[0;36m",
	      "\033[0;37m",
	      "\033[1;30m",
	      "\033[1;31m",
	      "\033[1;32m",
	      "\033[1;33m",
	      "\033[1;34m",
	      "\033[1;35m",
	      "\033[1;36m",
	      "\033[1;37m",
);

sub toansi($) {
  my $l=CL::unparse_colors($_[0]);
  $l=~s/\003(.)/$colormap[ord($1)-65]||""/ge;
  $l;
}

sub clog($) {
  if ($Conf::timedlog) {
    print $logfile &CL::gettime_ms()," ",($Conf::ansi_log?toansi($_[0]):CL::strip_colors($_[0])),
		  "\n" if $logfilename;
  } else {
    print $logfile ($Conf::ansi_log?toansi($_[0]):CL::strip_colors($_[0])),
		  "\n" if $logfilename;
  }
}

sub timestamp($) {
  print $logfile "######## ",scalar localtime,":: ",$_[0],"\n" if $logfilename;
}

sub logopen(;$) {
  if ($logfilename) {
    my $tmp=$logfilename;
    timestamp("Closing log file.");
    close($logfile);
    $logfile=undef;
    $logfilename=undef;
    CL::msg("Stopped logging to {$tmp}.");
  }
  return if ($#_<0);
  my $f=shift;
  $logfile=gensym();
  if (open($logfile,">>$f")) {
    CL::msg("Logging to {$f}.");
    $logfilename=$f;
    timestamp("Opened log file.");
  } else {
    CL::err("Can't open {$f}.");
  }
}

sub new {
  my $class=shift;
  CL::msg("Connecting to $_[0]:$_[1]...");
  if ($_[2]) {
    $class->SUPER::new($_[0],$_[1],1,$_[2]);
  } else {
    $class->SUPER::new($_[0],$_[1]);
  }
}

my MUD $sock=undef;
my $sock_conn=0;
my @triggers; # the trigger list
%tags=();

sub setlp {
  $sock ? $sock->set_lp($_[0]) : undef;
}

sub sockinfo {
  $sock ? $sock->info() : (0,0,0);
}

sub tcpinfo {
  $sock ? $sock->tcp_info(1) : undef;
}

sub ptcpinfo {
  $sock ? $sock->tcp_info(0) : undef;
}

sub forall(&) {
  my $func=shift;

  for my $t (@triggers) { &$func($t) }
}

sub i_add_trigger($) {
  my $t=shift;

  my $i;
  for ($i=0;$i<=$#triggers;$i++) {
    last if ($t->[5] > $triggers[$i][5]);
  }
  splice(@triggers,$i,0,$t);
  for $i (@{$t->[6]}) {
    $tags{$i}=[] if (!$tags{$i});
    push(@{$tags{$i}},$t);
  }
}

sub remove_trigger($) {
  my $tag=shift;

  return if (!$tags{$tag});
  my (%tl,%wl);
  for (@{$tags{$tag}}) {
    $tl{$_}=$_;
    for (@{$_->[6]}) {
      $wl{$_}=1;
    }
  }
  @triggers=grep { !$tl{$_} } @triggers;
  for my $w (keys %wl) {
    @{$tags{$w}}=grep { !$tl{$_} } @{$tags{$w}};
    delete $tags{$w} unless @{$tags{$w}};
  }
}

sub enable_trigger($) {
  return if (!$tags{$_[0]});
  for my $t (@{$tags{$_[0]}}) {
    $t->[1]=1;
    $t->[9]=0;
  }
}

sub disable_trigger($) {
  return if (!$tags{$_[0]});
  for my $t (@{$tags{$_[0]}}) {
    $t->[1]=0;
  }
}

my $tid=1;

sub parse_flags($) {
  my $f=shift;
  my $en=1;
  my $gag=0;
  my $fall=0;
  my $mul=0;
  my $prio=1000;
  my $max=0;
  my $save=$Conf::save_stuff;
  my @tags;

  if ($f =~ /^([^:]*):(.*)$/) {
    $f=$1;
    @tags=split(/,/,$2);
  }
  if (!@tags) {
    my $a=\@tags;
    $a="$a";
    push(@tags,$1) if ($a =~ /\(0x(.*)\)/);
  }
  if ($f =~ s/x(\d+)//) {
    $max=$0+$1;
  }
  if ($f =~ /(\d+)/) {
    $prio=0+$1;
  }
  for my $l (split(//,$f)) {
    if ($l eq "-") {
      $en=0;
    } elsif ($l eq "g") {
      $gag=1;
    } elsif ($l eq "f") {
      $fall=1;
    } elsif ($l eq "m") {
      $mul=1;
    } elsif ($l eq "n") {
      $save=0;
    } elsif ($l eq "s") {
      $save=1;
    }
  }
  ($en,$prio,\@tags,$fall,$gag,$mul,$save,$max);
}

sub make_flags($) {
  my $t=shift;
  my $r="";
  $r.="-" unless $t->[1];
  $r.="m" if $t->[2];
  $r.="f" if $t->[7];
  $r.="g" if $t->[8];
  $r.=$t->[5];
  $r.="x$t->[12]" if $t->[12]>0;
  $r.=":";
  $r.=join(",",@{$t->[6]});
}

sub add_trigger($$$$) {
  my $pattern=shift;
  my $subst=shift;
  my $code=shift;
  my $flags=shift;
  my ($en,$prio,$tags,$ft,$gag,$mul,$save,$max)=parse_flags($flags);
  i_add_trigger([qr/$pattern/,$en,$mul,$subst,$code,$prio,$tags,$ft,$gag,0,$save,$pattern,$max]);
}

sub load_trigs {
  my ($c,$p,$a,$s,$f);
  for my $l (split(/\n/,$_[0])) {
    next unless length($l);
    ($c,$p,$a,$s,$f)=split(/\t/,$l);
    $a=undef unless $c eq "1" || $c eq "3";
    $s=undef unless $c eq "2" || $c eq "3";
    add_trigger($p,defined($s)?Parser::parse_string($s):undef,$a,"s$f");
  }
}

sub save_trigs {
  join("\n",map { join("\t",chr(ord('0')+((defined($_->[3])?2:0)|(defined($_->[4])?1:0))),$_->[11],defined($_->[4])?$_->[4]:"",defined($_->[3])?Parser::format_string($_->[3]):"",make_flags($_)) } grep { $_->[10] && !(defined($_->[4]) && ref($_->[4])) } @triggers);
}

sub conn($$;$) {
  $sock->close if ($sock);
  $sock=undef;
  $sock_conn=0;
  $sock=new MUD(@_);
}

sub closesock() {
  return if (!$sock);
  $sock->close();
}

sub cleanup() {
  LE::echo(1);
  LE::reset(1,0);
  LE::setprompt(CL::parse_colors($Conf::defprompt));
  $::hoststatus="*not connected*";
  $sock=undef;
  $Ticker::tc->stop;
  ::call_hook('disconnect');
}

sub closed($) {
  my MUD $self=shift;
  CL::msg("$self->{rh}:$self->{rp}: connection closed.");
  timestamp("$self->{rh}:$self->{rp}: connection closed.");
  cleanup;
}

sub remclosed($) {
  my MUD $self=shift;
  CL::msg("$self->{rh}:$self->{rp}: connection closed by foreign host.");
  timestamp("$self->{rh}:$self->{rp}: connection closed by foreign host.");
  cleanup;
}

sub echo($$) {
  my MUD $self=shift;
  my $e=shift;
  LE::echo($e);
}

sub bell($) {
  my MUD $self=shift;
  ::sndevent("MudBeep");
}

my $lle=0;
my $lp;
my $lpd=1;
my $lpl=1;

sub remprompt() {
  $lp=undef;
  $lpd=$lpl=1;
}

sub connected($$) {
  my MUD $self=shift;
  CL::msg("Connected to $self->{rh}:$self->{rp}.");
  timestamp("Connected to $self->{rh}:$self->{rp}.");
  my $mname="$self->{rh}:$self->{rp}";
  $::hoststatus=$mname;
  $sock=$self;
  $sock_conn=1;
  $lle=0;
  $lp=undef;
  $lpd=$lpl=1;
  LE::setprompt('');
  ::call_hook('connect',$mname);
}

sub line($$) {
  my MUD $self=shift;
  my $cline=shift;
  my $line=CL::strip_colors($cline);

  local *_=\$line;
  local *;=\$cline;
  $P::logline=$cline;

  my $gag=0;

  for my $t (@triggers) {
    pos($line)=0;
    if ($t->[1] && $line =~ /$t->[0]/g) {
      $t->[1]=0 if ($t->[12]>0 && ++$t->[9]>=$t->[12]);
      if (defined($t->[3])) { # replace
	my ($l,$s,$csb);
	do {
	  $l=length($&);
	  $s=pos($line)-$l;
	  $csb=CL::parse_colors(Parser::subst_vars($t->[3],[$&,$1,$2,$3,$4,$5,$6,$7,$8,$9]));
	  substr($line,$s,$l)=CL::strip_colors($csb);
	  substr($cline,$s<<1,$l<<1)=$csb;
	  pos($line) = $s + (length($csb)>>1);
	} while $t->[2] && $line =~ /$t->[0]/g;
      }
      if (defined($t->[4])) {
	if (ref($t->[4])) {
	  try {
	    &{$t->[4]}; # perl code is run as is
	  } catch {
	    my $et=shift;
	    my $ei=shift||"";
	    CL::err("$et: $ei");
	  } "...";
	} else {
	  Parser::run_commands($t->[4],[$&,$1,$2,$3,$4,$5,$6,$7,$8,$9]);
	}
      }
      $gag=1 if ($t->[8]);
      last if (!$t->[7]);
    }
  }
  if (!$lpl) {
    clog($lp);
    $lpl=1;
  }
  $P::logline=$cline if $Conf::logsub && defined($P::logline);
  clog($P::logline) if defined($P::logline);
  return if ($gag);
  if (!length($cline)) {
    return if ($lle);
    $lle=1;
  } else {
    $lle=0;
  }
  if (!$lpd) {
    CL::toutput(0,$lp);
    $lpd=1;
  }
  CL::toutput(0,$cline);
}

sub prompt($$) {
  my MUD $self=shift;
  my $prompt=shift;
  
  $prompt=CL::parse_colors(::call_hook('prompt',CL::unparse_colors($prompt)));
  return if (defined($lp) && $lp eq $prompt);
  if (!$lpd) {
    CL::toutput(0,$lp);
  }
  if (!$lpl) {
    clog($lp);
  }
  $lp=$prompt;
  $lpd=$lpl=0;
  LE::setprompt($lp);
}

sub lognl() {
  if (LE::gecho) {
    clog($lp . CL::parse_colors(LE::input));
  } else {
    clog($lp);
  }
}

my $sent=0;

sub sent(;$) { $sent=0 if (@_); $sent }

my $repr=0;
sub repr(;$) { if (@_) { $repr=shift; } $repr }

sub sendl($) {
  if ($sock && $sock_conn) {
    my $text=::call_hook('send',$_[0]);
    $sent=1;
    $sock->writeln($text);
    if ($repr) {
      LE::snewline($text);
      $lpd=1;
      if (LE::gecho) {
        clog($lp . CL::parse_colors(LE::input));
      } else {
	clog($lp);
      }
    }
  } else {
    CL::warn("Can't send text: not connected to a server.");
  }
}

package MUD_helper;

use Ex;

sub TIESCALAR {
  my $class=shift;
  my $self;
  bless \$self,$class;
}

sub FETCH {
  my ($pkg,$file,$line)=caller;
  throw "Reading \$: is not allowed at $file line $line";
}

sub STORE {
  $;=CL::parse_colors($_[1]);
  $_=CL::strip_colors($;);
}

tie $:,'MUD_helper';

1;
