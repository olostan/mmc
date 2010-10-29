package main;

use strict;
use integer;
use locale;

use vars qw($initfile $lasttmp $hoststatus %sndevents);

use RStream; # this redirects error messages on use

use CL;
use LE;
use Keymap;
use Parser;
use MUD;
use CMD;
use Conf;
use Ex;
use DCommand;
use UAPI;
use Status;
use Ticker;

BEGIN { require("Text::ParseWords") if $^O eq "MSWin32" }

# do some trickery for Win32
if ($^O eq "MSWin32") {
  %Config::Config=(libpth=>'',ldlibpthname=>undef,path_sep=>'/');
  $INC{"Config.pm"}="[BUILTIN]:Config.pm";
}

sub getopt($$) {
  my $oh=$_[1];
  my %of=map { length($_)==1 ? ($_,1) : (substr($_,0,1),2) } split(/(?!:)/,$_[0]);
  while (@ARGV) {
    if ($ARGV[0] =~ /^--/) { shift(@ARGV); return undef; }
    if ($ARGV[0] =~ /^-$/) { shift(@ARGV); return "Invalid option: '-'"; }
    if ($ARGV[0] =~ /^-(.+)/) {
      my $os=$1;
      shift(@ARGV);
      while (length($os)>0) {
	my $o=substr($os,0,1);
	$os=substr($os,1);
	if (!$of{$o}) { return "Invalid option: '$o'"; }
	if ($of{$o}==1) {
	  $oh->{$o}=1;
	} else {
	  if (length($os)>0) {
	    push(@{$oh->{$o}},$os);
	    $os='';
	  } else {
	    if (@ARGV) {
	      push(@{$oh->{$o}},shift(@ARGV));
	    } else {
	      return "Option '$o' requires an argument";
	    }
	  }
	}
      }
    } else {
      return undef;
    }
  }
}

my $prefix='';
sub prefix(;$) { $prefix=$_[0] if defined($_[0]); $prefix }
my $interactive=1;
my $force_prefix_off=0;
my $default_noprefix=0;
my $status_default_noprefix=' ';

my $windownum;
my $wininfo='';
my $curwin=0;
my $lastmask=0;
sub mkwininfo(;$) {
  $CL::winfo&=~(1<<$curwin);
  return unless $lastmask!=$CL::winfo || $_[0];
  $wininfo='';
  for (my $i=0;$i<10 && length($wininfo)<4;++$i) {
    next if $i==$curwin;
    $wininfo.=length($wininfo)==3 ? '>' : $i+1 if $CL::winfo & (1<<$i);
  }
  $wininfo=",\003P$wininfo\003C" if $wininfo;
  $windownum="[" . ($curwin+1) . $wininfo . "]";
  $lastmask=$CL::winfo;
}
sub window_handler {
  $curwin=$_[0];
  mkwininfo(1);
}
sub window_active() {
  my $i=0;
  my $mask=$CL::winfo;
  $mask &= ~(1<<$curwin);
  while ($i<10 && !($mask & (1<<$i))) { ++$i }
  return unless $i<10;
  CL::gotowin($i);
  window_handler($i);
}

sub curwin() { $curwin }

my $sv_ptcpinfo;

sub run_one_command($) {
    no strict 'refs';

    my $cmd=shift;

    if ($cmd->[2]==1) {
	# check for speedwalks here
	return if CMD::speedwalk($cmd->[3]);
	if ($force_prefix_off || $interactive && !$Conf::prefixall) {
	  MUD::sendl($cmd->[3]);
	} else {
	  MUD::sendl($prefix . $cmd->[3]);
	}
    } elsif ($cmd->[2]==2) {
	# run alias here
	++$interactive;
	if (defined(&{"U::$cmd->[3]"})) {
	  &{"U::$cmd->[3]"}(@{$cmd}[4..$#$cmd]);
	} else {
	  CL::err("$cmd->[3]: no such alias.");
	}
	--$interactive;
    } elsif ($cmd->[2]==3) {
	# run command here
	++$interactive;
	if (defined(&{"CMD::cmd_$cmd->[3]"})) {
	  &{"CMD::cmd_$cmd->[3]"}(@{$cmd}[4..$#$cmd]);
	} else {
	  CL::err("$cmd->[3]: no such command.");
	}
	--$interactive;
    }
}

sub run_command($) {
    my $cmd=shift;

    if ($cmd->[1]>0 && $cmd->[0]>0) {
	new DCommand([$cmd->[0],$cmd->[1]&~0x40000000,sub { run_one_command($cmd) },$cmd,$cmd->[1]&0x40000000]);
    } elsif ($cmd->[0]>0) {
      try {
	while ($cmd->[0]--) {
	    run_one_command($cmd);
	}
      } catch {
	CL::err("$_[0]: $_[1]");
      } "...";
    }
}

sub call_hook($$) {
  my $hn=shift;
  my $hv=shift;
  return $hv if !$P::_hooks{$hn};
  my $ret;
  try {
    $ret=&{$P::_hooks{$hn}->[0]}($hv);
  } catch {
    CL::err("$_[0]: $_[1]");
  } '...';
  return defined($ret) ? $ret : $_[0];
}

sub newline {
    my $inp=::call_hook('input',LE::input);
    LE::newline;
    LE::reset(1,1),return unless defined $inp;
    MUD::lognl();
    MUD::sent(0);
    $interactive=0;
    Parser::run_commands($inp);
    $interactive=1;
    if (MUD::sent()) {
	LE::reset(0,1);
	MUD::remprompt;
    } else {
	LE::reset(1,1);
    }
}

sub newline_noprefix {
  ++$force_prefix_off;
  newline();
  --$force_prefix_off;
}

sub setup_newlines {
  if ($default_noprefix) {
    Keymap::sysbind("C-M",\&newline_noprefix);
    Keymap::sysbind("M-C-M",\&newline);
  } else {
    Keymap::sysbind("C-M",\&newline);
    Keymap::sysbind("M-C-M",\&newline_noprefix);
  }
}

sub set_noprefix {
  if (@_) {
    $default_noprefix=! !$_[0];
  } else {
    $default_noprefix=!$default_noprefix;
  }
  $status_default_noprefix=$default_noprefix ? '*' : ' ';
  setup_newlines();
}

sub complete_listgen {
    my $pref=shift;
    my @cm=Parser::parse_commands($pref,1);
    return if !@cm;
    my $lws=shift(@cm);
    @cm=@{$cm[$#cm]};
    return if $#cm<3;
    if ($cm[2]==3 || $cm[2]==2) {
      for (my $i=3;$i<=$#cm;$i++) {
	$cm[$i]=Parser::subst_vars($cm[$i],[]);
      }
      if ($cm[2]==3) {
	$cm[3]="$Conf::char$cm[3]";
	$lws-=length($Conf::char) if ($#cm==3);
      }
      return (substr($pref,$lws),CMD::complete(@cm[3..$#cm]));
    }
    $cm[3] =~ /^(.*?)(\S*)$/;
    @cm=length($1)?($1,$2):($2);
    return ($cm[-1],CMD::complete(@cm));
}

my %sechandlers;

sub set_section_handler($$;$) {
  my $name=shift;
  my $rh=shift;
  my $wh=shift;
  $sechandlers{$name}=[$rh,$wh];
}

sub load_rc_file($) {
  local $_;
  my $fn=shift;
  {
    package P;

    do $fn;
  }
  die $@ if $@;
  my ($sn,$sc,$nh);
  if (defined(*P::DATA{IO}) && !eof(P::DATA)) { # load extra conf sections
    while (<P::DATA>) { # find section start
      chomp;
  lp: next if (!/^\[(..*)\]$/);
      if (!$sechandlers{$1} || !$sechandlers{$1}->[0]) {
	CL::warn("No handler for section {$1}, skipping this section.");
	next;
      }
      $sn=$1;
      $sc="";
      $nh=undef;
      while (<P::DATA>) {
	chomp;
	next if (/^\s*$/);
	if (/^\[(..*)\]$/) {
	  $nh=$_;
	  last;
	}
	s/^\\//;
	if ($sc) {
	  $sc.="\n";
	  $sc.=$_;
	} else {
	  $sc=$_;
	}
      }
      &{$sechandlers{$sn}->[0]}($sc);
      if ($nh) {
	$_=$nh;
	goto lp;
      }
    }
    close(P::DATA);
  }
  1;
}

sub save_rc_file($) {
  local $_;
  local *RF;
  local *OF;
  my $fn=shift;
  my $tmp="$fn.tmp";
  $lasttmp=$tmp;
  if (-e $tmp) {
    CL::err("Can't write {$fn}: temporary file {$tmp} already exists.");
    return;
  }
  if (!open(RF,"> $tmp")) {
    CL::err("Can't write {$fn}: can't create a temproary file {$tmp}: $!.");
    return;
  }
  my ($k,$v);
  while (($k,$v)=each %sechandlers) {
    $v->[2]=0;
  }
  if (open(OF,"< $fn")) {
    while (<OF>) { # copy the perl code at the start
      chomp;
      last if ($_ eq "__DATA__");
      print RF $_,"\n";
    }
    print RF "__DATA__\n";
    my ($sn,$sc,$nh);
    while (<OF>) { # find section start
      chomp;
  lp: next if (!/^\[(..*)\]$/);
      $sn=$1;
      $sc="";
      $nh=undef;
      while (<OF>) {
	chomp;
	next if (/^\s*$/);
	if (/^\[(..*)\]$/) {
	  $nh=$_;
	  last;
	}
	s/^\\//;
	if ($sc) {
	  $sc.="\n";
	  $sc.=$_;
	} else {
	  $sc=$_;
	}
      }
      print RF "[$sn]\n";
      if ($sechandlers{$sn} && $sechandlers{$sn}->[1]) { # call the save handler
	my $sd=&{$sechandlers{$sn}->[1]}($sc);
	$sd =~ s/\n([\\\[])/\n\\$1/sg;
	print RF $sd,"\n";
	$sechandlers{$sn}->[2]=1;
      } else { # no handler, copy the section verbatim
	$sc =~ s/\n([\\\[])/\n\\$1/sg;
	print RF $sc,"\n";
      }
      if ($nh) {
	$_=$nh;
	goto lp;
      }
    }
    close(OF);
  } else {
    print RF "__DATA__\n";
  }
  while (($k,$v)=each %sechandlers) {
    if (!$v->[2] && $v->[1]) {
      print RF "[$k]\n",&{$v->[1]}(""),"\n";
    }
  }
  close(RF);
  rename($tmp,$fn);
  $lasttmp=undef;
  return 1;
}

sub load_aliases {
  no strict 'refs';
  for my $l (split(/\n/,$_[0])) {
    my ($n,$a)=split(/\t/,$l,2);
    next unless $n;
    if (defined($a)) {
      *{"U::$n"}=sub { Parser::run_commands($a,[undef,@_]) };
      @{"U::$n"}=($a);
    } else {
      $U::_nosave_aliases{$n}=1;
    }
  }
}

sub save_aliases {
  no strict 'refs';
  join("\n",(map { join("\t",$_,${"U::$_"}[0]) } sort { $a cmp $b } grep { defined(&{"U::$_"}) && defined(@{"U::$_"}) && !$U::_nosave_aliases{$_} } keys %U::),keys %U::_nosave_aliases);
}

sub load_vars {
  no strict 'refs';
  for my $l (split(/\n/,$_[0])) {
    my ($n,$v)=split(/\t/,$l,2);
    next unless $n;
    if (!defined($v)) {
      $U::_nosave_vars{$n}=1;
    } else {
      ${"U::$n"}=$v;
    }
  }
}

sub save_vars {
  no strict 'refs';
  join("\n",(map { join("\t",$_,${"U::$_"}) } sort { $a cmp $b } grep { defined(${"U::$_"}) && !$U::_nosave_vars{$_} && !$::bad_vars{$_} } keys %U::),keys %U::_nosave_vars);
}

sub load_hooks {
  for my $l (split(/\n/,$_[0])) {
    my ($h,$v)=split(/\t/,$l,2);
    next unless $h;
    $P::_hooks{$h}=[sub { Parser::run_commands($v,[undef,@_]); $_[0] },$v];
  }
}

sub save_hooks {
  join("\n",map { join("\t",$_,$P::_hooks{$_}->[1]) } grep { defined($P::_hooks{$_}->[1]) } keys %P::_hooks);
}

%sndevents=("Editor" => "beep", "MudBeep" => "beep"); # default beep

sub load_sounds {
  for my $l (split(/\n/,$_[0])) {
    my ($ev,$file)=split(/\t/,$l,2);
    next unless $ev;
    $sndevents{$ev}=$file;
  }
}

sub save_sounds {
  join("\n",map { join("\t",$_,$sndevents{$_}) } sort { $a cmp $b } grep { defined($sndevents{$_}) } keys %sndevents);
}

sub sndevent {
  if (defined($_[0]) && defined($sndevents{$_[0]})) {
    if ($sndevents{$_[0]} eq "beep") {
      CL::playsound(""); # default beep
    } else {
      CL::playsound($sndevents{$_[0]});
    }
  }
}

sub load_run {
  for my $l (split(/\n/,$_[0])) {
    Parser::run_commands($l);
  }
}

sub usage {
  my $msg=<<EOF ;
Usage: mmc [-chknprt?] [-s type] [-i dir] [-f history_file] [config file]
    -c          show completion status in the status line
    -f file     save history to a separate file
    -h, -?      prints this message
    -i dir      add dir to the module search path
    -k          show keys in the status line
    -n          don't display connection information in the status line
    -p          show input prefix in the status line
    -r          disable updating setup file on exit
    -s type     sets the status line type, type can be 0, 1 or 2 (default)
    -t          show tcp info if available
EOF
  CL::msg($_) for split(/\n/,$msg);
}

sub run {
    my %opt;

    # mangle args on win32
    if ($^O eq "MSWin32") {
      if (@ARGV && defined($ARGV[0])) {
	@ARGV=Text::ParseWords::parse_line(qr/\s+/,0,$ARGV[0]);
      } else {
	@ARGV=();
      }
    }

    my $em=getopt("cf:h?i:knprs:t",\%opt);
    if ($em) {
      CL::err($em);
      return;
    }
    if ($opt{h} || $opt{'?'}) {
      usage();
      return;
    }
    $Conf::status_type=pop(@{$opt{s}}) if $opt{s};

    # adjust include paths
    push(@INC,@{$opt{i}}) if $opt{i};
    # add the dir from which we were executed to the include path
    push(@INC,$::rundir) if $::rundir;

    # now find the init file
    if ($^O eq "MSWin32") { # use different rules for win32
      for (".mmc4rc", "mmc.ini") {
	for my $p ($::rundir,$ENV{HOME},".") {
	  next unless $p;
	  my $v=$p;
	  $v =~ s=[\\/]*$==;
	  $initfile="$v/$_" if -f "$v/$_";
	  last if $initfile;
	}
      }
      # if we still didnt find one, create one near the executable
      $initfile="$::rundir/mmc.ini" if $::rundir && !$initfile;
    } else {
      $initfile=($ENV{MMC}||$ENV{HOME}||".") . "/.mmc4rc";
    }
    $initfile=shift(@ARGV) if @ARGV; # always use an init file from command line

    CL::set_vattr($Conf::statusbg);
    Status::setcolors($Conf::statusfg);
    LE::setcolors($Conf::incolor,$Conf::iccolor);
    CL::statusconf($Conf::status_type,$Conf::status_height);

    for my $i (0 .. 63) { $::bad_vars{chr($i)}=1 }
    for my $i (91 .. 95) { $::bad_vars{chr($i)}=1 }
    for my $i (123 .. 127) { $::bad_vars{chr($i)}=1 }
    CL::msg(CL::get_version());
    set_section_handler("history",sub { LE::hinit($_[0]) }, sub { LE::hget(); }) unless $opt{f};
    set_section_handler("triggers",\&MUD::load_trigs,\&MUD::save_trigs);
    set_section_handler("vars",\&load_vars,\&save_vars);
    set_section_handler("aliases",\&load_aliases,\&save_aliases);
    set_section_handler("keys",\&Keymap::load_keys,\&Keymap::save_keys);
    set_section_handler("hooks",\&load_hooks,\&save_hooks);
    set_section_handler("run",\&load_run,undef);
    set_section_handler("sounds",\&load_sounds,\&save_sounds);
    set_section_handler("complete",\&CMD::load_completions,\&CMD::save_completions);
    set_section_handler("tablist",\&CMD::load_tablist,\&CMD::save_tablist);
    *P::set_section_handler=\&set_section_handler;
    *P::new_sv=\&Status::new_sv;
    *P::new_svy=\&Status::new_svy;

    Keymap::init;
    CMD::compinit;

    setup_newlines();
    Keymap::sysbind("C-I",LE::make_completion_function(\&complete_listgen));
    Keymap::sysbind("RDR",sub { LE::refresh(); Status::refresh(); });

    LE::reset();
    LE::setprompt(CL::parse_colors($Conf::defprompt));
    $windownum="[1]";
    Status::new_sv($windownum,8);
    $hoststatus="*not connected*";
    Status::new_sv($hoststatus,20) if !$opt{n};
    if ($opt{t}) {
      Status::new_sv($sv_ptcpinfo,15);
      new DCommand([0,500,sub { $sv_ptcpinfo=MUD::ptcpinfo||"[*N/A*]" },[0,0,1,"tcpinfo"]]);
    }
    LE::svinit() if $opt{c};
    if ($opt{p}) {
      Status::new_sv($status_default_noprefix,1);
      Status::new_sv($prefix,10);
    }
    if ($opt{k}) {
      Status::new_sv($Keymap::sv_key,15);
      $Keymap::show_key=1;
    }

    # launch our status line ticker
    new Ticker2;

    # ok, after everything seems to be in place, replace output streams
    tie *STDOUT, 'RStream';
    tie *STDERR, 'RStream', 1;
    { local $^W=0; undef $SIG{__DIE__}; } # remove our die printer

    if ($initfile) {
      CL::msg("Loading {$initfile}...");
      try {
	$initfile=undef if !load_rc_file($initfile);
      } catch {
	my $et=shift;
	my $ei=shift||"";
	CL::err("$et: $ei");
	$initfile=undef;
      } "...";
    }
    if ($opt{f}) {
      $opt{f}=pop(@{$opt{f}});
      CL::msg("Loading history from {$opt{f}}...");
      if (open(HISTORY,"< $opt{f}")) {
	local $/=undef;
	my $history=<HISTORY>;
	close(HISTORY);
	LE::hinit($history);
      } else {
	CL::warn("Can't read {$opt{f}}: $!");
      }
    }
    window_handler($curwin);
    Status::go;
    while (!CL::loop_finished()) {
      last if CL::main_loop_iteration()<0;
      mkwininfo;
    }
    LE::reset(0,1);
    if ($initfile && !$opt{r}) {
      CL::msg("Saving {$initfile}...");
      try {
	unlink($lasttmp) if !save_rc_file($initfile) && $lasttmp;
      } catch {
	my $et=shift;
	my $ei=shift||"";
	CL::err("$et: $ei");
	unlink($lasttmp) if $lasttmp;
      } "...";
    }
    if ($opt{f}) {
      CL::msg("Saving history to {$opt{f}}...");
      if (open(HISTORY,"> $opt{f}")) {
	print HISTORY LE::hget();
	close(HISTORY);
      } else {
	CL::warn("Can't write to {$opt{f}}: $!");
      }
    }
    # close log
    MUD::logopen();
}

1;
