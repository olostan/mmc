package Keymap;

use strict;
use integer;

use locale;
use vars qw($show_key $sv_key);

use Ex;
use CL;
use LE;
use MUD;
use Parser;

###############################################################################
# keymaps and the terminal input handler
#
my %syskeymap;

sub sysbind($$) {
    $syskeymap{$_[0]}=$_[1] if $_[0] && $_[1];
}

my %keymap;

sub bindkey($$) {
    my $code=$_[0];
    if ($code) {
	$code =~ s/C-(.)$/C-\U$1/;
	if (defined($_[1])) {
	    $keymap{$code}=$_[1];
	} else {
	    delete $keymap{$code};
	}
    }
}

sub getproc($) {
    my $v=shift;
    return undef if (!$v);
    return $keymap{$v} if (exists($keymap{$v}));
    return $syskeymap{$v} if (exists($syskeymap{$v}));
    undef;
}

my  $literal;
$show_key=0;
$sv_key='';

sub termhan($) {
    my $kc=shift;
    try {
	$sv_key=$kc if ($show_key && $kc ne "RDR");
	if ($literal) {
	  $literal=0;
	  $kc =~ s/^C-(.)$/chr(ord($1)-64)/e;
	  if (length($kc)==1) {
	    LE::self_insert_command($kc);
	  }
	  return;
	}
	if ($keymap{$kc}) {
	  MUD::repr(1) if ($Conf::send_verbose);
	  if (ref($keymap{$kc})) {
	    &{$keymap{$kc}}($kc);
	  } else {
	    Parser::run_commands($keymap{$kc},[$kc,$kc]);
	  }
	  MUD::repr(0);
	} elsif ($syskeymap{$kc}) { &{$syskeymap{$kc}}($kc) }
    } catch {
	my $et=shift;
	my $ei=shift||"";
	CL::err("$et: $ei");
    } "...";
}

my %reftoname;

sub getname($) {
  my $cmd=shift;
  return $reftoname{$cmd} if exists $reftoname{$cmd};
  return LE::getname($cmd);
}

my %commands=(
  "window_active" => [ sub { ::window_active() } , "M--" ],
  "window_1" => [ sub { CL::gotowin(0); ::window_handler(0) }, "M-1" ],
  "window_2" => [ sub { CL::gotowin(1); ::window_handler(1) }, "M-2" ],
  "window_3" => [ sub { CL::gotowin(2); ::window_handler(2) }, "M-3" ],
  "window_4" => [ sub { CL::gotowin(3); ::window_handler(3) }, "M-4" ],
  "window_5" => [ sub { CL::gotowin(4); ::window_handler(4) }, "M-5" ],
  "window_6" => [ sub { CL::gotowin(5); ::window_handler(5) }, "M-6" ],
  "window_7" => [ sub { CL::gotowin(6); ::window_handler(6) }, "M-7" ],
  "window_8" => [ sub { CL::gotowin(7); ::window_handler(7) }, "M-8" ],
  "window_9" => [ sub { CL::gotowin(8); ::window_handler(8) }, "M-9" ],
  "window_10" => [ sub { CL::gotowin(9); ::window_handler(9) }, "M-0" ],
  "scrollback_line_up" => [ sub { &CL::sbup(1) }, "C-pgup" ],
  "scrollback_line_down" => [ sub { &CL::sbdown(1) }, "C-pgdn" ],
  "scrollback_page_up" => [ sub { &CL::sbup(0) }, "pgup" ],
  "scrollback_page_down" => [ sub { &CL::sbdown(0) }, "pgdn" ],
  "scrollback_end" => [ sub { &CL::sbdown(-1) }, "C-Q" ],
  "redraw" => [ sub { &CL::redraw() }, "C-L" ],
  "literal" => [ sub { $literal=1 }, "C-V" ],
  "newline" => [ \&::newline ],
  "newline_noprefix" => [ \&::newline_noprefix ],
);

sub getcmd($) {
  no strict 'refs';
  my $n=shift;
  return $commands{$n}[0] if exists $commands{$n};
  return \&{"LE::$n"} if defined &{"LE::$n"};
  undef;
}

###############################################################################
# initialize the default system keymap
#
sub init() {
    sysbind("bs",\&LE::backward_delete_char);
    sysbind("C-H",\&LE::backward_delete_char);
    sysbind("del",\&LE::delete_char);
    sysbind("left",\&LE::backward_char);
    sysbind("C-B",\&LE::backward_char);
    sysbind("right",\&LE::forward_char);
    sysbind("C-F",\&LE::forward_char);
    sysbind("home",\&LE::beginning_of_line);
    sysbind("C-A",\&LE::beginning_of_line);
    sysbind("end",\&LE::end_of_line);
    sysbind("C-E",\&LE::end_of_line);
    sysbind("C-U",\&LE::kill_whole_line);
    sysbind("C-K",\&LE::kill_line);
    sysbind("M-k",\&LE::backward_kill_line);
    sysbind("M-d",\&LE::delete_word);
    sysbind("M-del",\&LE::delete_word);
    sysbind("M-bs",\&LE::backward_delete_word);
    sysbind("M-C-H",\&LE::backward_delete_word);
    sysbind('C-@',\&LE::set_mark_command);
    sysbind('C-X',\&LE::exchange_point_and_mark);
    sysbind("C-W",\&LE::kill_region);
    sysbind("C-O",\&LE::copy_region_as_kill);
    sysbind("C-Y",\&LE::yank);
    sysbind("ins",\&LE::yank);
    sysbind("up",\&LE::up_history);
    sysbind("C-P",\&LE::up_history);
    sysbind("down",\&LE::down_history);
    sysbind("C-N",\&LE::down_history);
    sysbind("M-p",\&LE::history_search_backward);
    sysbind("M-n",\&LE::history_search_forward);
    sysbind("M-_",\&LE::insert_last_word);
    sysbind("M-/",\&LE::dabbrev_expand);

    sysbind("C- ",\&LE::set_mark_command); # Win32 specific

    sysbind("M-up",\&LE::history_search_backward);
    sysbind("M-down",\&LE::history_search_forward);

    sysbind("M-h",\&LE::newline_noexec);
    sysbind("C-down",\&LE::newline_noexec);
    sysbind("M-f",\&LE::forward_word);
    sysbind("M-b",\&LE::backward_word);
    sysbind("C-right",\&LE::forward_word);
    sysbind("C-left",\&LE::backward_word);

    for (my $i=32;$i<256;$i++) {
	$syskeymap{chr($i)}=\&LE::self_insert_command;
    }

    my ($k,$v);
    while (($k,$v)=each %commands) {
      $reftoname{$v->[0]}=$k;
      sysbind($v->[1],$v->[0]);
    }

    CL::set_term_handler(\&termhan);
}

###############################################################################
# nicely print all mappings
#
sub printall($;$) {
    my $formatter=shift;
    my $flag=shift;
    my @km;

    my ($k,$v,$n);
    while (($k,$v)=each %keymap) {
	next if ($v eq \&LE::self_insert_command);
	push(@km,[$k,&$formatter($v)]) if ($k);
    }
    if ($flag) {
      while (($k,$v)=each %syskeymap) {
	next if (defined($keymap{$k}) || $v eq \&LE::self_insert_command);
	push(@km,[$k,&$formatter($v)]) if ($k);
      }
    }
    for my $k (sort { $a->[0] cmp $b->[0] } @km) {
	CL::msg("{$k->[0]}=$k->[1]");
    }
}

###############################################################################
# save/restore user-level bindindgs
#
sub load_keys {
  for my $l (split(/\n/,$_[0])) {
    my ($k,$cmd)=split(/\t/,$l,2);
    next unless $k;
    if ($cmd =~ /^\@(.*)/s) {
      $cmd=getcmd($1);
      next unless $cmd;
    }
    bindkey($k,$cmd);
  }
}

sub save_keys {
  my ($k,$v);
  my @r;
  while (($k,$v)=each %keymap) {
    if (ref($v)) {
      my $c;
      next unless $c=getname($v);
      push(@r,"$k\t\@$c");
    } else {
      push(@r,"$k\t$v");
    }
  }
  join("\n",@r);
}

1;
