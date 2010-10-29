package CMD;

use strict;
use locale;

use CL;
use MUD;
use Ex;
use Keymap;
use Conf;
use DCommand;

my %complist=();
my %abbrev;
my %ambig;

sub mkabbrev() {
  no strict 'refs';
  my ($name,$glob);
  my %scm;
  while (($name,$glob)=each %CMD::) {
    $scm{$1}=\&{"CMD::$name"} if ($name =~ /^cmd_(.+)$/ &&
					      defined(&{"CMD::$name"}));
  }
  while (($name,$glob)=each %scm) {
    next if (length($name)<=1);
    my $ab=substr($name,0,length($name)-1);
    while (length($ab)>0) {
      if ($scm{$ab} || $abbrev{$ab}) {
	$ambig{$ab}=1;
      } else {
	$abbrev{$ab}=$glob;
      }
      $ab=substr($ab,0,length($ab)-1);
    }
  }
  while (($name,$glob)=each %ambig) {
    delete $abbrev{$name};
  }
  while (($name,$glob)=each %abbrev) {
    *{"CMD::cmd_$name"}=$glob;
  }
}

sub print_alias($) {
  no strict 'refs';
  my $an=shift;
  if (defined(@{"U::$an"})) {
    my $d=\@{"U::$an"};
    CL::msg(($U::_nosave_aliases{$an} ? "*" : "") . "{$an}={" . $d->[0] . "}");
  } else {
    CL::msg(($U::_nosave_aliases{$an} ? "*" : "") . "{$an}={" . (\&{"U::$an"}) . "}");
  }
}

sub print_hook($$) {
  my $an=shift;
  my $ac=shift;
  CL::msg("{$an}={" . ($ac->[1]||$ac->[0]) . "}");
}

sub glob2re($) {
  my $a='';
  my $b='';
  my $g=shift;
  if ($g =~ /^\*/) {
    $g=substr($g,1);
  } else {
    $a="^";
  }
  if ($g =~ /\*$/) {
    $g=substr($g,0,length($g)-1);
  } else {
    $b='$';
  }
  $g =~ s/\./\\./g;
  $g =~ s/\*/.*/g;
  $g =~ s/\?/./g;
  qr/$a$g$b/;
}

sub addrp($) {
  my $r="$_[0]";
  if ($r =~ /\(0x(.*)\)/) { $1 } else { $r }
}

sub format_cmd($) {
  my $cmd=shift;

  if ($cmd->[2]==3) {
    if ($#$cmd>=4) {
      "${Conf::char}$cmd->[3] {" . join("} {",@{$cmd}[4..$#$cmd]) . "}";
    } else {
      "${Conf::char}$cmd->[3]";
    }
  } elsif ($cmd->[2]==2) {
    if ($#$cmd>=4) {
      "$cmd->[3] {" . join("} {",@{$cmd}[4..$#$cmd]) . "}";
    } else {
      "$cmd->[3]";
    }
  } else {
    $cmd->[3];
  }
}

sub format_km_cmd($) {
  my $cmd=shift;
  return "{$cmd}" if (!ref($cmd));
  my $s=Keymap::getname($cmd);
  return "\@$s" if ($s);
  $cmd;
}

sub fit($$) {
  my $s=shift;
  my $w=shift;
  return ' ' x $w if (!defined($s));
  return $s . (' ' x ($w-length($s))) if (length($s)<$w);
  return $s if (length($s)==$w);
  return substr($s,0,$w-1) . ">";
}

sub format_tcmd($) {
  my $c=shift;
  return "$c" if (ref($c));
  $c =~ s/\003(.)/\&$1/sg;
  "{$c}"
}

sub format_trigger($) {
  my $t=shift;
  my $w=(CL::twidth()-24)/4;

  CL::msg(
      ($t->[1]?' ':'-')
    . ($t->[10]?'S':' ')
    . ($t->[8]?'G':' ')
    . ($t->[7]?'F':' ')
    . ($t->[2]?'M':' ')
    . ' '
    . fit("$t->[5]",5)
    . ' '
    . fit($t->[12]>0 ? "$t->[12]" : "inf",3)
    . ' '
    . fit("$t->[11]",$w)
    . ' '
    . fit(Parser::format_string($t->[3]),$w)
    . ' '
    . fit(format_tcmd($t->[4]),$w)
    . ' '
    . fit(join(",",@{$t->[6]}),$w)
  );
}

sub format_trigger_long($) {
  my $t=shift;
  CL::toutput(0,CL::parse_colors("\003BPattern: \003J$t->[11]"));
  CL::toutput(0,CL::parse_colors("\003CSubst  : \003L" . Parser::format_qstring($t->[3]))) if defined $t->[3];
  CL::toutput(0,CL::parse_colors("\003CCommand: \003O" . format_tcmd($t->[4]))) if defined $t->[4];
  CL::toutput(0,CL::parse_colors("\003CFlags  : \003K" . MUD::make_flags($t)));
  CL::toutput(0,CL::parse_colors("\003CMatched: \003M" . $t->[9] . "/" . $t->[12] . " time(s)")) if $t->[12]>0;
}

###############################################################################
# programmable completion
my @complist;

sub parse_compspec($) {
  my $spec=shift;
  return if ($spec !~ /^([cnNp])(.)(.*)$/);
  my ($word,$delim)=($1,$2);
  $spec=$3;
  my ($pat,$list,$suff)=split(/\Q$delim\E/,$spec);
  return if (!defined($pat) || !defined($list));
  $suff=' ' if (!defined($suff));
  my $type;
  if ($list =~ /^\$(.+)$/) {
    $type='V';
    $list=$1;
  } elsif ($list =~ /^\((.*)\)$/) {
    $type='L';
    $list=[split(/\s+/,$1)];
  } elsif ($list =~ /^\@(.+)$/) {
    $type='Z';
    $list=$1;
  } elsif ($list =~ /^([achHptuv])(:(.+))?$/) {
    $type=$1;
    $list=$3;
  } else {
    return;
  }
  my $cpat;
  if ($word eq 'p') {
    if ($pat =~ /^(\d+)-(\d+)$/) {
      $pat=[0+$1,0+$2];
    } elsif ($pat =~ /^-(\d+)$/) {
      $pat=[0,$1];
    } elsif ($pat =~ /^(\d+)-$/) {
      $pat=[0+$1,8192];
    } elsif ($pat =~ /^\d+$/) {
      $pat=[0+$pat,0+$pat];
    } elsif ($pat eq "*") {
      $pat=[0,8192];
    } else {
      return;
    }
  } else {
    $cpat= length($pat) ? qr/$pat/ : qr/.*/;
  }
  ($word,$pat,$type,$list,$suff,$delim,$cpat);
}

sub format_cspec($) {
  my $c=shift;
  my $s=$c->[0] . $c->[5];
  if ($c->[0] eq "p") {
    my $from=$c->[1][0];
    my $to=$c->[1][1];
    if ($from==0 && $to==8192) {
      $s.="*";
    } elsif ($from == $to) {
      $s.=$from;
    } elsif ($from==0) {
      $s.="-$to";
    } elsif ($to==8192) {
      $s.="$from-";
    } else {
      $s.="$from-$to";
    }
  } else {
    $s .= $c->[1];
  }
  $s.=$c->[5];
  if ($c->[2] eq "L") {
    $s.="(";
    $s.=join(" ",@{$c->[3]});
    $s.=")";
  } elsif ($c->[2] eq "V") {
    $s.="\$$c->[3]";
  } elsif ($c->[2] eq 'Z') {
    $s.="\@$c->[3]";
  } else {
    $s.=$c->[2];
    $s.=":$c->[3]" if (defined($c->[3]));
  }
  $s.="$c->[5]$c->[4]" if defined($c->[4]);
}

my @hooknames=('tick','pretick','prompt','connect','disconnect','input','send');

my %comptypes=(
'a' => sub {
  no strict 'refs';
  my $prefix=shift;
  $prefix=qr/^\Q$prefix\E/;
  my $select=shift;
  if (defined($select)) {
    my $pat=qr/$select/;
    sort grep { defined(&{"U::$_"}) && $_ =~ $prefix && $_ =~ $pat } keys %U::;
  } else {
    sort grep { defined(&{"U::$_"}) && $_ =~ $prefix } keys %U::;
  }
},

'v' => sub {
  no strict 'refs';
  my $prefix=shift;
  $prefix=qr/^\Q$prefix\E/;
  my $select=shift;
  if (defined($select)) {
    my $pat=qr/$select/;
    sort grep { defined(${"U::$_"}) && $_ =~ $prefix && $_ =~ $pat } keys %U::;
  } else {
    sort grep { defined(${"U::$_"}) && $_ =~ $prefix } keys %U::;
  }
},

'u' => sub {
  my $prefix=shift;
  $prefix=qr/^(?i:\Q$prefix\E)/;
  my $select=shift;
  if (defined($select)) {
    my $pat=qr/$select/;
    sort grep { $_ =~ $prefix && $_ =~ $pat } keys %complist;
  } else {
    sort grep { $_ =~ $prefix } keys %complist;
  }
},

'h' => sub {
  my $prefix=shift;
  $prefix=qr/^\Q$prefix\E/;
  my $select=shift;
  if (defined($select)) {
    my $pat=qr/$select/;
    sort grep { $_ =~ $prefix && $_ =~ $pat } keys %P::_hooks;
  } else {
    sort grep { $_ =~ $prefix } keys %P::_hooks;
  }
},

'H' => sub {
  my $prefix=shift;
  $prefix=qr/^\Q$prefix\E/;
  my $select=shift;
  if (defined($select)) {
    my $pat=qr/$select/;
    grep { $_ =~ $prefix && $_ =~ $pat } @hooknames;
  } else {
    grep { $_ =~ $prefix } @hooknames;
  }
},

't' => sub {
  my $prefix=shift;
  $prefix=qr/^\Q$prefix\E/;
  my $select=shift;
  if (defined($select)) {
    my $pat=qr/$select/;
    sort grep { $_ =~ $prefix && $_ =~ $pat } keys %MUD::tags;
  } else {
    sort grep { $_ =~ $prefix } keys %MUD::tags;
  }
},

'p' => sub {
  my $prefix=shift;
  $prefix=qr/^\Q$prefix\E/;
  my $select=shift;
  if (defined($select)) {
    my $pat=qr/$select/;
    sort grep { $_ =~ $prefix && $_ =~ $pat } map { m/\(0x(.*)\)/; $1 } keys %DCommand::cmdlist;
  } else {
    sort grep { $_ =~ $prefix } map { m/\(0x(.*)\)/; $1 } keys %DCommand::cmdlist;
  }
},

'c' => sub {
  no strict 'refs';
  my $prefix=shift;
  my $np=$prefix;
  $np=$1 if ($prefix =~ /^\Q$Conf::char\E(.*)$/);
  $np=qr/^\Q$np\E/;
  my $select=shift;
  my @cl=map { substr($_,4) } grep { defined(&{"CMD::$_"}) && /^cmd_/ } keys %CMD::;
  if (defined($select)) {
    my $pat=qr/$select/;
    sort map { $Conf::char . $_ } grep { !$abbrev{$_} && !$ambig{$_} && $_ =~ $np && $_ =~ $pat } @cl;
  } else {
    sort map { $Conf::char . $_ } grep { !$abbrev{$_} && !$ambig{$_} && $_ =~ $np } @cl;
  }
},

'L' => sub {
  my $prefix=shift;
  my $select=shift;
  my $pat=qr/^(?i:\Q$prefix\E)/;
  grep { $_ =~ $pat } @$select;
},

'Z' => sub {
  my $prefix=shift;
  my $select=shift;
  my $pat=qr/^(?i:\Q$prefix\E)/;
  local $^W=0;
  no strict 'refs';
  grep { $_ =~ $pat } @{"P::$select"};
},

'V' => sub {
  my $prefix=shift;
  my $select=shift;
  local $^W=0;
  no strict 'refs';
  my $pat=qr/^(?i:\Q$prefix\E)/;
  grep { $_ =~ $pat } split(' ',${"U::$select"});
},
);

sub complete_spec($$$) {
  my $prefix=shift;
  my $type=shift;
  my $select=shift;
  if ($comptypes{$type}) {
    &{$comptypes{$type}}($prefix,$select);
  } else {
    return;
  }
}

sub complete(@) {
  return () if !@_;
  my $command=Parser::subst_vars($_[0],[]);
  for my $c (@complist) {
    if ($command =~ $c->[0]) {
      my $prefix=$_[$#_];
      my $word=Parser::subst_vars($prefix,[]);
      for my $spec (@{$c->[1]}) {
	if ($spec->[0] eq "c") { # current word completion
	  next unless ($word =~ $spec->[6]);
	  return ($spec->[4],complete_spec($word,$spec->[2],$spec->[3]));
	} elsif ($spec->[0] eq "n") { # next word completion
	  next unless ($#_>0);
	  my $pword=Parser::subst_vars($_[$#_-1],[]);
	  next unless ($pword =~ $spec->[6]);
	  return ($spec->[4],complete_spec($word,$spec->[2],$spec->[3]));
	} elsif ($spec->[0] eq "N") { # next word completion
	  next unless ($#_>1);
	  my $pword=Parser::subst_vars($_[$#_-2],[]);
	  next unless ($pword =~ $spec->[6]);
	  return ($spec->[4],complete_spec($word,$spec->[2],$spec->[3]));
	} elsif ($spec->[0] eq "p") { # positional completion
	  next unless ($#_>=$spec->[1][0] && $#_<=$spec->[1][1]);
	  return ($spec->[4],complete_spec($word,$spec->[2],$spec->[3]));
	} else {
	  return;
	}
      }
    }
  }
  return;
}

sub load_completions {
  @complist=();
  for my $c (split("\n",$_[0])) {
    my @cl=split("\t",$c);
    my $p=shift(@cl);
    push(@complist,[qr/$p/,[map { [ parse_compspec($_) ] } @cl],$p]);
  }
}

sub save_completions {
  join("\n",map { join("\t",$_->[2],map { format_cspec($_) } @{$_->[1]}) } @complist);
}

sub load_tablist {
  %complist=map { ($_,1) } split("\n",$_[0]);
}

sub save_tablist {
  join("\n",keys %complist);
}

my @defcomplist=(
  ['^/v',     'p/1/v/'],
  ['^/unv',   'p/1/v/'],
  ['^/unal',  'p/1/a/'],
  ['^/unac',  'p/1-/t/'],
  ['^/en',    'p/1-/t/'],
  ['^/di',    'p/1-/t/'],
  ['^/h',     'p/1/H/'],
  ['^/unh',   'p/1/h/'],
  ['^/ki',    'p/1-/p/'],
  ['^/',      'p/0/c/ '],
  ['^',	      'p/0/a/ ','c//u/ '],
);

sub compinit() {
  for my $c (@defcomplist) {
    my $cm=shift(@$c);
    push(@complist,[qr/$cm/,[map { [parse_compspec($_)] } @$c],$cm]);
  }
}
###############################################################################
# Speedwalks
my %pathmap=('u' => 'u', 'd' => 'd', 'n' => 'n', 's' => 's', 'e' => 'e', 'w' => 'w');
my %revmap=('u' => 'd', 'd' => 'u', 'n' => 's', 's' => 'n', 'e' => 'w', 'w' => 'e');

sub pathmap($) { $pathmap{$_[0]} }
sub revmap($) { $revmap{$_[0]} }

sub walkf($) {
  my $p=shift;
  $p =~ s/(\d+)(.)/$2 x $1/ge;
  map { $pathmap{$_} } split(//,$p);
}

sub walkb($) {
  my $p=shift;
  $p =~ s/(\d+)(.)/$2 x $1/ge;
  map { $pathmap{$revmap{$_}} } reverse split(//,$p);
}

sub speedwalk($) {
  my @l;
  if ($_[0] =~ /^\.\.(.*)/) {
    @l=walkb($1);
  } elsif ($_[0] =~ /^\.(.*)/) {
    @l=walkf($1);
  } else {
    return;
  }
  if (@l<=5) {
    MUD::sendl(join("\n",@l));
  } else {
    my @dl;
    while (@l) {
      push(@dl,join("\n",splice(@l,0,5)));
    }
    new DCommand([$#dl+1,$Conf::speedwalk_delay,
		      sub { MUD::sendl(shift(@dl)) },
		      [undef,undef,1,"speedwalk"]]);
  }
  return 1;
}

###############################################################################
# User callable commands
sub cmd_quit {
  $::initfile=undef if $_[0] eq "-a";
  CL::msg("Exiting...");
  CL::post_quit_message();
}

sub cmd_connect {
  if ($#_<1) {
    CL::warn("connect: usage: /connect <host> <port> [local address]");
  } else {
    my ($h,$p,$l)=@_;
    try {
      MUD::conn($h,$p,$l);
    } catch {
    } "SocketError";
  }
}

sub cmd_dc {
  MUD::closesock();
}

sub cmd_alias {
  no strict 'refs';
  if ($#_<0) { # dump all aliases
    CL::msg("Defined aliases:");
    for my $name (sort keys %U::) {
      print_alias($name) if (defined(&{"U::$name"}));
    }
  } elsif ($#_==0) { # print alias definition
    my $re=glob2re($_[0]);
    my $f=0;
    for (sort grep { /$re/ && defined(&{"U::$_"}) } keys %U::) {
      print_alias($_);
      $f=1;
    }
    CL::msg("No match(es) found for {$_[0]}.") if (!$f);
  } else { # define a new alias
    my $n=shift;
    my $al=join(" ",@_);
    *{"U::$n"}=sub { Parser::run_commands($al,[undef,@_]) };
    @{"U::$n"}=($al);
    CL::msg(($U::_nosave_aliases{$n} ? "*" : "") . "{$n} aliases {$al}.") if ($Conf::verbose);
  }
}

sub cmd_unalias {
  no strict 'refs';
  if ($#_>=0) {
    my $re=glob2re($_[0]);
    my $f=0;
    for (grep { /$re/ } keys %U::) {
      if (defined(&{"U::$_"})) {
	undef(&{"U::$_"});
	undef(@{"U::$_"});
	$f=1;
	CL::msg("{$_} is no longer an alias.") if ($Conf::verbose);
      }
    }
    CL::msg("No match(es) found for {$_[0]}.") if (!$f && $Conf::verbose);
  }
}

sub cmd_variable {
  no strict 'refs';
  if ($#_<0) { # dump all variables
    CL::msg("Defined variables:");
    for my $name (sort { $a cmp $b } keys %U::) {
      CL::msg(($U::_nosave_vars{$name} ? "*" : "") . "{$name}={" . ${"U::$name"} . "}") if (defined(${"U::$name"}) && !$::bad_vars{$name});
    }
  } elsif ($#_==0) { # print variable
    my $re=glob2re($_[0]);
    my $f=0;
    for (sort grep { /$re/ && defined(${"U::$_"}) && !$::bad_vars{$_} } keys %U::) {
      CL::msg(($U::_nosave_vars{$_} ? "*" : "") . "{$_}={" . ${"U::$_"} . "}");
      $f=1;
    }
    CL::msg("No match(es) found for {$_[0]}.") if (!$f);
  } else { # define a new variable
    if ($_[0] !~ /^[A-Za-z_]\w*$/) {
      CL::warn("{$_[0]}: invalid variable name.");
    } else {
      ${"U::$_[0]"}=$_[1];
      CL::msg(($U::_nosave_vars{$_[0]} ? "*" : "") . "{$_[0]} is now set to {$_[1]}.") if ($Conf::verbose);
    }
  }
}

sub cmd_set {
  no strict 'refs';
  if ($#_<1) {
    CL::warn("set: usage: /set <name> <value>");
  } else {
    my ($n,$v)=@_;
    if ($n !~ /^[A-za-z_]\w*$/) {
      CL::warn("{$n}: invalid variable name.");
    } else {
      if ($v =~ /^\([()0-9\/+\-*]+\)$/) {
	${"U::$n"}=eval($v);
      } else {
	${"U::$n"}=$v;
      }
    }
  }
}

sub cmd_unvariable {
  no strict 'refs';
  while (@_) {
    my $n;
    my $re=glob2re($n=shift);
    my $f=0;
    for (grep { /$re/ } keys %U::) {
      if (defined(${"U::$_"})) {
	eval { untie(${"U::$_"}) };
	undef(${"U::$_"});
	$f=1;
	CL::msg("{$_} is no longer a variable.") if ($Conf::verbose);
      }
    }
    CL::msg("No match(es) found for {$n}.") if (!$f && $Conf::verbose);
  }
}

*cmd_unset = *cmd_unset = \&cmd_unvariable;

sub cmd_showme {
  CL::msg(join(" ",@_));
}

sub cmd_echo {
  CL::toutput(0,CL::parse_colors(join(" ",@_)));
}

sub cmd_wecho {
  if ($#_<0) {
    CL::warn("wecho: usage: /wecho <num> [text] ...");
  } else {
    CL::toutput(0+shift,CL::parse_colors(join(" ",@_)));
  }
}

sub cmd_ps {
  no integer;
  my $now=CL::gettime();
  for my $c (sort { $a->{when} <=> $b->{when} } values %DCommand::cmdlist) {
    my $tm=1000.0*($c->{when}-$now);
    $tm=0 if ($tm<0);
    CL::msg(sprintf("%-10s %4d %6d %s",addrp($c),$c->{cmd}[0],$tm,
		    $c->{cmd}[3] ? format_cmd($c->{cmd}[3]) : ''));
  }
}

sub cmd_kill {
  my $id=shift||$DCommand::lastdcmd;
  for my $c (values %DCommand::cmdlist) {
    $c->remove if ("$c" =~ /$id/)
  }
}

sub cmd_char {
  if ($#_<0) {
    CL::msg("Command char is {$Conf::char}.");
  } else {
    CL::msg("Command char is now {" . Parser::char($_[0]) . "}");
  }
}

sub cmd_bind {
  if ($#_<0) {
    Keymap::printall(\&format_km_cmd);
  } elsif ($#_==0) {
    if ($_[0] eq "-a") {
      Keymap::printall(\&format_km_cmd,1);
      return;
    }
    my $h=Keymap::getproc($_[0]);
    if ($h) {
      CL::msg("{$_[0]}=" . format_km_cmd($h));
    } else {
      CL::msg("{$_[0]} is not bound to anything.");
    }
  } else {
    my $k=shift;
    my $c=join(" ",@_);
    if ($c && substr($c,0,1) eq '@') {
      $c=substr($c,1);
      my $sub=Keymap::getcmd($c);
      if (defined($sub)) {
	$c=$sub;
      } else {
	CL::warn("No such builtin command '$c'.");
	return;
      }
    }
    Keymap::bindkey($k,$c);
  }
}

sub cmd_unbind {
  if ($#_>=0) {
    Keymap::bindkey($_[0],undef);
  } else {
    CL::warn("unbind: usage: /unbind <key>");
  }
}

sub cmd_bell {
  CL::playsound("");
}

sub cmd_cr {
  MUD::sendl('');
}

sub cmd_nop {
}

sub cmd_action {
  if ($#_<0) {
    MUD::forall(\&format_trigger);
  } elsif ($#_>=1) {
    MUD::add_trigger($_[0],undef,$_[1],$_[2]||'');
  } else {
    if ($_[0] eq "-l") {
      MUD::forall(\&format_trigger_long);
    } else {
      CL::warn("action: usage: /action [pattern command [options]]");
    }
  }
}

sub cmd_substitute {
  if ($#_>=1) {
    MUD::add_trigger($_[0],Parser::parse_string($_[1]),undef,$_[2]||'');
  } else {
    CL::warn("substitute: usage: /substitute <pattern> <string> [options]");
  }
}

sub cmd_gag {
  if ($#_>=0) {
    MUD::add_trigger($_[0],undef,undef,'g' . ($_[1]||''));
  } else {
    CL::warn("gag: usage: /gag <pattern> [options]");
  }
}

sub cmd_xsub {
  if ($#_>=2) {
    MUD::add_trigger($_[0],Parser::parse_string($_[1]),$_[2],$_[3]||'');
  } else {
    CL::warn("xsub: usage: /xsub <pattern> <subst string> <command> [options]");
  }
}

sub cmd_unaction {
  if ($#_>=0) {
    for my $t (@_) { MUD::remove_trigger($t) }
  } else {
    CL::warn("unaction: usage: /unaction <tag>");
  }
}

sub cmd_enable {
  if ($#_>=0) {
    for my $t (@_) { MUD::enable_trigger($t) }
  } else {
    CL::warn("enable: usage: /enable <tag>");
  }
}

sub cmd_disable {
  if ($#_>=0) {
    for my $t (@_) { MUD::disable_trigger($t) }
  } else {
    CL::warn("disable: usage: /disable <tag>");
  }
}

sub cmd_log {
  if ($#_>=0) {
    my $l=shift;
    if ($l eq "-c") {
      $Conf::ansi_log=1;
      $l=shift;
    } elsif ($l ne "off") {
      $Conf::ansi_log=0;
    }
    if ($l eq "off") {
      MUD::logopen();
    } else {
      MUD::logopen($l);
      if (@_) { # write scrollback buffer to log
	my $window=$_[0];
	my ($i,$l,$f);
	for ($i=0;;++$i) {
	  last unless ($l,$f)=CL::fetchline($window,$i);
	  MUD::clog($l) if !$f;
	}
      }
    }
  } else {
    CL::warn("log: usage: /log [-c] [filename|'off'] [window number]");
  }
}

sub cmd_lastlog {
  if ($#_>=0) {
    my $pattern=qr/$_[0]/;
    my $window=::curwin();
    my ($i,$l,$f);
    CL::toutput($window,CL::parse_colors("\003PLastlog\003I:"),1);
    for ($i=0;;++$i) {
      last unless ($l,$f)=CL::fetchline($window,$i);
      CL::toutput($window,$l,1) if !$f && CL::strip_colors($l) =~ $pattern;
    }
    CL::toutput($window,CL::parse_colors("\003PEnd of Lastlog"),1);
  } else {
    CL::warn("lastlog: usage: /lastlog <pattern>");
  }
}

sub cmd_tabadd {
  if ($#_<0) {
    CL::warn("tabadd: usage: /tabadd <word> ...");
  } else {
    for my $w (@_) {
      $complist{$w}=1;
    }
  }
}

sub cmd_tabdelete {
  if ($#_<0) {
    CL::warn("tabdelete: usage: /tabdelete <word> ...");
  } else {
    for my $w (@_) {
      delete $complist{$w} if (exists($complist{$w}));
    }
  }
}

sub cmd_tablist {
  CL::msg(join(" ",keys %complist));
}

sub cmd_perl {
  my $r=join(" ",@_);
  {
    package P;
    no strict 'vars';
    eval($r);
  }
  if ($@) {
    my $e=$@;
    if (ref($e)) {
      CL::err("$e->[0]: $e->[1]");
    } else {
      chomp($e);
      $e=~tr/\000-\037/ /s;
      CL::err("#perl: $e");
    }
  }
}

sub cmd_tickset {
  $Ticker::tc->restart;
}

sub cmd_ticksize {
  if ($#_<0) {
    CL::warn("tickset: usage: /tickset <time>");
  } else {
    if ($_[0]>20) {
      $Ticker::tc->setsize(0+$_[0]);
    } else {
      CL::warn("tickset: invalid tick size");
    }
  }
}

sub cmd_tick {
  $Ticker::tc->info;
}

sub cmd_tickoff {
  $Ticker::tc->stop;
}

sub cmd_hook {
  if ($#_<0) { # dump all hooks
    CL::msg("Defined hooks:");
    my ($k,$v);
    while (($k,$v)=each %P::_hooks) {
      print_hook($k,$v);
    }
  } elsif ($#_==0) { # print hook definition
    my $re=glob2re($_[0]);
    my $f=0;
    for (sort grep { /$re/ } keys %P::_hooks) {
      print_hook($_,$P::_hooks{$_});
      $f=1;
    }
    CL::msg("No match(es) found for {$_[0]}.") if (!$f);
  } else { # define a new hook
    my $n=shift;
    my $al=join(" ",@_);
    $P::_hooks{$n}=[sub { Parser::run_commands($al,[undef,@_]); $_[0] },$al];
    CL::msg("{$n} is set to {$al}.") if ($Conf::verbose);
  }
}

sub cmd_unhook {
  if ($#_>=0) {
    my $re=glob2re($_[0]);
    my $f=0;
    for (grep { /$re/ } keys %P::_hooks) {
	delete $P::_hooks{$_};
	$f=1;
	CL::msg("{$_} removed.") if ($Conf::verbose);
    }
    CL::msg("No match(es) found for {$_[0]}.") if (!$f && $Conf::verbose);
  }
}

sub cmd_path {
  if ($#_<0) {
    for my $p (sort keys %pathmap) {
      CL::msg("$p=$pathmap{$p} [$revmap{$p}]");
    }
  } else {
    for my $p (@_) {
      if ($p =~ /^(.),(.)=([^,]+),([^,]+)$/) {
	$pathmap{$1}=$3;
	$pathmap{$2}=$4;
	$revmap{$1}=$2;
	$revmap{$2}=$1;
      }
    }
  }
}

sub cmd_svar {
  if ($#_<2) {
    CL::warn("svar: usage: /svar <name> <y> <width> [color]");
  } else {
    no strict 'refs';
    my $vn=shift;
    my $y=shift;
    my $w=shift;
    my $c=shift;
    if ($vn !~ /^[A-Za-z_]\w*$/) {
      CL::warn("{$_[0]}: invalid variable name.");
      return;
    }
    Status::new_svy(${"U::$vn"},$y,$w,$c);
  }
}

sub cmd_if {
  my $cond=shift;
  my $then=shift;
  my $else=shift;

  my $c;
  if ($cond =~ /^\([()0-9+\-\/\/*<>=]+\)$/) {
    {
      package U;
      no strict 'vars';
      $c=eval($cond);
    }
    if ($@) {
      my $em=$@;
      chomp($em);
      $em=~tr/\000-\037/ /s;
      CL::err("/if: $em");
      return;
    }
  } else {
    $c=$cond;
  }
  if ($c) {
    Parser::run_commands($then) if (defined($then));
  } else {
    Parser::run_commands($else) if (defined($else));
  }
}

sub cmd_send {
  MUD::sendl(join(" ",@_));
}

sub cmd_info {
  my ($rin,$rout,$pin)=MUD::sockinfo();
  CL::msg("Raw in      :  $rin");
  CL::msg("Protocol in :  $pin");
  CL::msg("Raw out     :  $rout");
  CL::msg("Compression :  " . sprintf("%.1f",($rin ? $pin/$rin : 0)));
  my $tinfo=MUD::tcpinfo();
  CL::msg("TCP stats   :  $tinfo") if defined $tinfo;
}

sub cmd_version {
  CL::msg(CL::get_version());
}

sub cmd_lpdelay {
  if ($#_>=0) {
    CL::set_lp_delay($_[0]);
  } else {
    CL::warn("lpdelay: usage: /lpdelay <time>");
  }
}

sub cmd_complete {
  if ($#_<0) { # list all completions
    for my $c (@complist) {
      CL::msg($c->[2] . " " . join(" ",map { format_cspec($_) } @{$c->[1]}));
    }
  } elsif ($#_>0) {
    my $word=shift;
    my $cs=[qr/$word/,[],$word];
    for my $c (@_) {
      my @spec=parse_compspec($c);
      if (!@spec) {
	CL::err("Invalid completion specification: '$c'");
      } else {
	push(@{$cs->[1]},[@spec]);
      }
    }
    unshift(@complist,$cs);
  } else {
    CL::warn("complete: usage: /complete [word spec ...]");
  }
}

sub cmd_uncomplete {
  if ($#_<0) {
    CL::msg("uncomplete: usage: /uncomplete <pattern> ...");
    return;
  }
  for my $p (@_) {
    @complist=grep { $_->[2] ne $p } @complist;
  }
}

sub cmd_prefix {
  CL::msg("prefix is {" . ::prefix($_[0]) . "}");
}

sub cmd_system {
  if ($#_<0) {
    CL::warn("system: usage: /system <command>");
    return;
  }
  new SYS_Helper(join(' ',@_));
}

sub cmd_nosave {
  if ($#_<1 || ($_[0] !~ /^a/i && $_[0] !~ /^v/i)) {
    CL::warn("nosave: usage: /nosave <alias|var> <name>");
  } else {
    if ($_[0] =~ /^a/i) {
      $U::_nosave_aliases{$_[1]}=1;
    } else {
      $U::_nosave_vars{$_[1]}=1;
    }
  }
}

sub cmd_save {
  if ($#_<1 || ($_[0] !~ /^a/i && $_[0] !~ /^v/i)) {
    CL::warn("save: usage: /save <alias|var> <name>");
  } else {
    if ($_[0] =~ /^a/i) {
      delete $U::_nosave_aliases{$_[1]} if exists $U::_nosave_aliases{$_[1]};
    } else {
      delete $U::_nosave_vars{$_[1]} if exists $U::_nosave_vars{$_[1]};
    }
  }
}

sub cmd_soundevent {
  if ($#_<0) {
    CL::msg(sprintf("%-20s %s","Event","File"));
    CL::msg(sprintf("%-20s %s",$_,$::sndevents{$_})) for sort keys %::sndevents;
  } elsif ($#_<1) {
    CL::warn("soundevent: usage: /soundevent [<event> <filename>]");
  } else {
    $::sndevents{$_[0]}=$_[1];
  }
}

sub cmd_playsound {
  if ($#_<0) {
    CL::warn("playsound: usage: /playsound <event>");
  } else {
    if (defined($::sndevents{$_[0]})) {
      ::sndevent($_[0]);
    } else {
      CL::warn("playsound: no such event: '$_[0]'");
    }
  }
}

sub cmd_slowscroll {
  if ($#_<0) {
    CL::warn("slowscroll: usage: /slowscroll <delay>");
  } else {
    CL::slowscroll($_[0]);
  }
}

sub cmd_noprefix {
  my $arg="toggle";
  $arg=shift if @_;
  if ($arg eq "on") {
    ::set_noprefix(1);
  } elsif ($arg eq "off") {
    ::set_noprefix(0);
  } elsif ($arg eq "toggle") {
    ::set_noprefix();
  } else {
    CL::warn("noprefix: usage: /noprefix [on|off|toggle]");
  }
}

###############################################################################

mkabbrev();

package SYS_Helper;

use base 'CL::Socket';
use fields qw(window);
use CL;
use Ex;
use Symbol qw(gensym);

sub new {
  my $class=shift;
  my $cmd=shift;
  my $self=$class->SUPER::new_pipe($cmd);
  $self->{window}=::curwin();
  $self;
}

sub line($$) {
  my $self=shift;
  my $line=shift;
  CL::toutput($self->{window},$line);
}

1;
