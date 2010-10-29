package LE;
use strict;
use integer;
use locale;

use CL;
use Conf;

###############################################################################
# helper variables and functions first
#
sub MIN($$) { $_[0]<$_[1]?$_[0]:$_[1] }

my $inputcolor = 11;
my $inputccolor = 12;

my $state   =	0;
my $ipos    =	0;
my $vlen    =	0;
my $vpos    =	0;
my $plen    =	0;
my $prompt  =	"";
my $saved_prompt = "";
my $inp	    =	"";
my $ed_echo =	1;
my $mark    =	-1;
my $cbuf    =	"";
my @hl	    =	(undef);
my $hi	    =	0;
my $hsp	    =	"";
my $cstatus =	'';

my @complete	=   ();
my $compidx	=   -1;
my $comppref	=   "";
my $compsuf	=   "";

my %reftoname;

sub beep() {
  ::sndevent("Editor");
}

sub setcolors($$) {
  $inputcolor=$_[0];
  $inputccolor=$_[1];
}

sub hinit($) {
    my $data=shift;
    @hl=(undef,reverse grep { length } split(/\n/,$data));
}

sub hget() {
    join("\n",reverse @hl[1..$#hl]);
}

sub hadd($) {
    my $s=shift;
    return if (!length($s));
    $hl[0]=$s;
    for (my $i=1;$i<=$#hl;$i++) {
      if ($hl[$i] eq $s) {
	splice(@hl,$i,1);
	last;
      }
    }
    unshift(@hl,undef);
    splice(@hl,$#hl,1) if ($#hl>=10000);
}

sub hmove(;$$) {
    my $d=shift;
    my $s=shift;
    if (!$d) { $hi=0; return; }
    if ($d<0) {
      if ($hi<$#hl) {
	if (length($s)) {
	my $i=$hi+1;
	my $j=length($s);
	while ($i<=$#hl) {
	    if (substr($hl[$i],0,$j) eq $s) {
	      $hi=$i;
	      return $hl[$i];
	    }
	    $i++;
	  }
	} else {
	  return $hl[++$hi];
	}
      }
    } elsif ($d>0) {
      if ($hi>0) {
	if (length($s)) {
	my $i=$hi-1;
	my $j=length($s);
	while ($i>=0) {
	    if (substr($hl[$i],0,$j) eq $s) {
	      $hi=$i;
	      return $hl[$i];
	    }
	    $i--;
	  }
	} else {
	  return $hl[--$hi];
	}
      }
    }
    undef;
}

sub svinit() {
  Status::new_sv($cstatus,6);
}

sub dabbrev_listgen($) {
    my $pre=shift;
    $pre =~ /(\S*)$/;
    my $w=$1;
    my $l=length($w);
    my @result;
    $hl[0]=substr($pre,0,length($pre)-$l);
    for my $i (@hl) {
      push(@result,grep { length>=$l && substr($_,0,$l) eq $w } reverse(split(' ',$i)));
    }
    return ($w,' ',@result);
}

sub lscroll() {
    if ($vpos>0) {
      --$vpos;
      CL::tinsc($plen,substr($inp,$vpos,1),$inputcolor);
    }
}

sub rscroll() {
    if ($vpos<length($inp)) {
      ++$vpos;
      CL::tdelc($plen,1);
      if ($vpos+$vlen-1<length($inp)) {
	CL::twritenc($plen+$vlen-1,substr($inp,$vpos+$vlen-1,1),$inputcolor);
      }
    }
}

sub setcur() {
    if ($ed_echo) {
      CL::tmoveto($plen+($ipos-$vpos)) if ($ipos>=$vpos && $ipos<$vpos+$vlen);
    } else {
      CL::tmoveto($plen);
    }
}

sub redisplay() {
    CL::tmoveto($plen);
    CL::twritenc($plen,substr($inp,$vpos,MIN(length($inp)-$vpos,$vlen)),$inputcolor) if ($ed_echo);
    CL::tdeol();
    setcur;
}

sub fixcur() {
    return if (!$ed_echo);
    if ($ipos-$vpos<3) {
      if ($vpos>0) {
	if ($ipos-$vpos==2) {
	  lscroll;
	  setcur;
	} else {
	  $vpos=$ipos-3;
	  $vpos=0 if ($vpos<0);
	  redisplay;
	}
      }
    } elsif ($ipos>$vpos+$vlen-3) {
      if ($vpos+$vlen-2==$ipos) {
	rscroll;
	setcur;
      } else {
	$vpos=$ipos-$vlen+3;
	redisplay;
      }
    }
}
    
sub fixcura() {
    return if (!$ed_echo);
    if ($ipos-$vpos<3) {
      if ($vpos>0) {
	$vpos=$ipos-3;
	$vpos=0 if ($vpos<0);
      }
    } elsif ($ipos>$vpos+$vlen-3) {
      $vpos=$ipos-$vlen+3;
    }
    redisplay;
}

sub setprompt($) {
    $plen=CL::twidth()-15-1;
    $saved_prompt=$_[0];
    $prompt=substr($_[0],0,$plen*2);
    $plen=length($prompt)>>1;
    $vlen=CL::twidth()-$plen-1;
    CL::twrite(0,$prompt);
    CL::tdeol() if (!$ed_echo); # XXX shortcut
    fixcura;
}

sub reset(;$$) {
    hadd($inp) if ($ed_echo);
    $inp="";
    $vpos=$ipos=0;
    # $ed_echo=1;
    set_state(0);
    hmove(0);
    if (!$_[0]) {
      $plen=0;
      $prompt="";
    }
    $vlen=CL::twidth()-$plen-1;
    if ($_[1]) {
      CL::twrite(0,$prompt);
      CL::tdeol();
    }
}

sub refresh() {
  setprompt($saved_prompt);
}

sub echo($) {
    $ed_echo=$_[0];
    redisplay;
}

sub gecho() {
  $ed_echo;
}

sub set_state($) {
    if ($_[0]!=$state) {
      if ($state==1) {
	$hsp='';
	hmove(0);
      } elsif ($state==3) {
	$compidx=-1;
	@complete=();
	$cstatus='';
      }
      $state=$_[0];
      if ($state==1) {
	$hsp=$inp;
	$hl[0]=$inp;
	$hi=0;
      }
      1;
    } else {
      0;
    }
}

sub input {
    $inp;
}

sub generic_complete($) {
    my $listgen=shift;
    if (set_state(3)) {
      $compidx=-1;
      my $iprefix=substr($inp,0,$ipos);
      ($comppref,$compsuf,@complete)=&$listgen($iprefix);
    }
    if ($#complete>=0) {
      if ($compidx>=$#complete) {
	my $dc=length($complete[$compidx]);
	$dc+=length($compsuf);
	$mark-=$dc-length($comppref) if $mark>=$ipos;
	$ipos-=$dc;
	$inp=substr($inp,0,$ipos) . $comppref . substr($inp,$ipos+$dc);
	$ipos+=length($comppref);
	fixcura;
	set_state(0);
	beep();
      } else {
	my $dc=length($comppref);
	if ($compidx>=0) {
	  $dc=length($complete[$compidx]);
	  $dc+=length($compsuf);
	}
	++$compidx;
	$mark-=$dc if $mark>=$ipos;
	$ipos-=$dc;
	my $nw=$complete[$compidx];
	$nw.=$compsuf;
	$inp=substr($inp,0,$ipos) . $nw . substr($inp,$ipos+$dc);
	$mark+=length($nw) if $mark>=$ipos;
	$ipos+=length($nw);
	fixcura;
	$cstatus=($compidx+1) . "/" . ($#complete+1);
      }
    } else {
      set_state(0);
      beep();
    }
}

sub make_completion_function($) {
    my $listgen=shift;
    return sub { generic_complete($listgen) };
}

sub getname($) {
    $reftoname{$_[0]};
}

sub newline() {
  if (!$Conf::hideinput) {
    if ($ed_echo) {
      if ($Conf::fullinput) {
	CL::tnewline(0,$saved_prompt . CL::parse_colors("\003" . chr(ord('A')+$Conf::incolor) . $inp));
      } else {
	CL::tnewline(0,$prompt . CL::parse_colors("\003" . chr(ord('A')+$Conf::incolor) . substr($inp,0,$vlen)));
      }
    } else {
      if ($Conf::fullinput) {
	CL::tnewline(0,$saved_prompt);
      } else {
	CL::tnewline(0,$prompt);
      }
    }
  }
}

sub snewline($) {
  if ($ed_echo) {
    if ($Conf::fullinput) {
      CL::tnewline(0,$saved_prompt . CL::parse_colors("\003" . chr(ord('A')+$Conf::incolor) . $inp));
    } else {
      CL::toutput(0,$prompt . CL::parse_colors("\003" . chr(ord('A')+$Conf::incolor) . substr($_[0],0,$vlen)));
    }
  }
}

###############################################################################
# editor commands now
#
sub self_insert_command($) {
    my $c=substr($_[0],0,1);
    return if (!length($c));
    set_state(0);
    $inp=substr($inp,0,$ipos) . $c . substr($inp,$ipos);
    ++$mark if ($mark>=$ipos);
    ++$ipos;
    if ($ed_echo) {
      if ($ipos<length($inp)) {
	CL::tinsc(CL::curpos(),$c,$inputcolor);
      } else {
	CL::twritenc(CL::curpos(),$c,$inputcolor);
      }
      fixcur;
    }
}

sub delete_char {
    set_state(0);
    if ($ipos>=length($inp)) {
      beep();
      return;
    }
    $mark=-1 if $ipos==$mark;
    ++$mark if $mark>$ipos;
    $inp=substr($inp,0,$ipos) . substr($inp,$ipos+1);
    if ($ed_echo) {
      CL::tdelc(CL::curpos(),1);
      if (length($inp)>=$vpos+$vlen) {
	CL::twritenc($plen+$vlen-1,substr($inp,$vpos+$vlen-1,1),$inputcolor);
	setcur;
      }
    }
}

sub backward_delete_char {
    set_state(0);
    if ($ipos<=0) {
      beep();
      return;
    }
    --$ipos;
    $mark=-1 if $ipos==$mark;
    ++$mark if $mark>$ipos;
    $inp=substr($inp,0,$ipos) . substr($inp,$ipos+1);
    return if (!$ed_echo);
    if ($ipos>=$vpos+3) {
      CL::tleft();
      CL::tdelc(CL::curpos(),1);
      if (length($inp)>=$vpos+$vlen) {
	CL::twritenc($plen+$vlen-1,substr($inp,$vpos+$vlen-1,1),$inputcolor);
	setcur;
      }
    } else {
      fixcura;
    }
}

sub backward_char {
    #set_state(0);
    if ($ipos>0) {
      --$ipos;
      if ($ed_echo) {
	CL::tleft() if ($ipos>=$vpos);
	fixcur;
      }
    } else {
      beep();
    }
}

sub forward_char {
    #set_state(0);
    if ($ipos<length($inp)) {
      ++$ipos;
      if ($ed_echo) {
	CL::tright();
	fixcur;
      }
    } else {
      beep();
    }
}

sub beginning_of_line {
    #set_state(0);
    if ($ipos>0) {
      $ipos=0;
      if ($ed_echo) {
	setcur;
	fixcur;
      }
    }
}

sub end_of_line {
    #set_state(0);
    if ($ipos<length($inp)) {
      $ipos=length($inp);
      if ($ed_echo) {
	setcur;
	fixcur;
      }
    }
}

sub kill_whole_line {
    set_state(0);
    if (length($inp)) {
      $cbuf=$inp;
      $inp="";
      $ipos=0;
      $mark=-1;
      fixcura;
    }
}

sub kill_line {
    set_state(0);
    if ($ipos<length($inp)) {
      $mark=-1 if $mark>=$ipos;
      $cbuf=substr($inp,$ipos);
      $inp=substr($inp,0,$ipos);
      setcur,CL::tdeol() if $ed_echo;
    }
}

sub backward_kill_line {
    set_state(0);
    if ($ipos>0) {
      if ($mark<$ipos && $mark>=0) {
	$mark=-1;
      } else {
	$mark-=$ipos;
      }
      $cbuf=substr($inp,0,$ipos);
      $inp=substr($inp,$ipos);
      $ipos=0;
      fixcura;
    }
}

sub delete_word {
    set_state(0);
    if ($ipos<length($inp)) {
      if (substr($inp,$ipos) =~ /^(\s*\S*)/ && length($1)) {
	$cbuf=$1;
	if ($mark>=$ipos && $mark<$ipos+length($1)) {
	$mark=-1;
	} else {
	$mark-=length($1);
	}
	$inp=substr($inp,0,$ipos) . substr($inp,$ipos+length($1));
	fixcura;
      }
    } else {
      beep();
    }
}

sub backward_delete_word {
    set_state(0);
    if ($ipos>0) {
      if (substr($inp,0,$ipos) =~ /(\S*\s*)$/ && length($1)) {
	$cbuf=substr($inp,$ipos-length($1),length($1));
	$ipos-=length($1);
	if ($mark>=$ipos && $mark<$ipos+length($1)) {
	$mark=-1;
	} else {
	$mark-=length($1);
	}
	$inp=substr($inp,0,$ipos) . substr($inp,$ipos+length($1));
	fixcura;
      }
    } else {
      beep();
    }
}

sub set_mark_command {
    $mark=$ipos;
}

sub exchange_point_and_mark {
    #set_state(0);
    if ($mark>=0) {
      if ($mark!=$ipos) {
	my $tmp=$ipos;
	$ipos=$mark;
	$mark=$tmp;
	fixcura;
      }
    } else {
      beep();
    }
}

sub kill_region {
    set_state(0);
    if ($mark>=0) {
      return if ($mark==$ipos);
      my $s=$mark;
      my $e=$ipos;
      if ($mark>$ipos) {
	$s=$ipos;
	$e=$mark;
      }
      $cbuf=substr($inp,$s,$e-$s);
      $inp=substr($inp,0,$s) . substr($inp,$e);
      if ($s!=$ipos) {
	$ipos-=$e-$s;
      } else {
	$mark=$ipos;
      }
      fixcura;
    } else {
      beep();
    }
}

sub copy_region_as_kill {
    #set_state(0);
    if ($mark>=0) {
      return if ($mark==$ipos);
      my $s=$mark;
      my $e=$ipos;
      if ($mark>$ipos) {
	$s=$ipos;
	$e=$mark;
      }
      $cbuf=substr($inp,$s,$e-$s);
    } else {
      beep();
    }
}

sub yank {
    set_state(0);
    if (length($cbuf)) {
      $inp=substr($inp,0,$ipos) . $cbuf . substr($inp,$ipos);
      $ipos+=length($cbuf);
      fixcura;
    }
}

sub up_history {
    set_state(1);
    my $l=hmove(-1);
    if (defined($l)) {
      $mark=-1;
      $inp=$l;
      $ipos=length($inp);
      $vpos=0;
      fixcura;
    } else {
      beep();
    }
}

sub down_history {
    set_state(1);
    my $l=hmove(1);
    if (defined($l)) {
      $mark=-1;
      $inp=$l;
      $ipos=length($inp);
      $vpos=0;
      fixcura;
    } else {
      beep();
    }
}

sub history_search_backward {
    set_state(1);
    my $l=hmove(-1,$hsp);
    if (defined($l)) {
      $mark=-1;
      $inp=$l;
      $ipos=length($inp);
      $vpos=0;
      fixcura;
    } else {
      beep();
    }
}

sub history_search_forward {
    set_state(1);
    my $l=hmove(1,$hsp);
    if (defined($l)) {
      $mark=-1;
      $inp=$l;
      $ipos=length($inp);
      $vpos=0;
      fixcura;
    } else {
      beep();
    }
}

sub insert_last_word {
    set_state(0);
    if ($#hl>0) {
      if ($hl[1] =~ /(\S+)\s*$/) {
	$inp=substr($inp,0,$ipos) . $1 . substr($inp,$ipos);
	$ipos+=length($1);
	fixcura;
      }
    } else {
      beep();
    }
}

sub dabbrev_expand {
    generic_complete(\&dabbrev_listgen);
}

sub forward_word {
  if ($ipos < length($inp)) {
    if (substr($inp,$ipos) =~ /^(\S*\s*)/s && length($1)) {
      $ipos+=length($1);
      fixcura;
    }
  } else {
    beep();
  }
}  

sub backward_word {
  if ($ipos>0) {
    if (substr($inp,0,$ipos) =~ /(\S*\s*)$/s && length($1)) {
      $ipos-=length($1);
      fixcura;
    }
  } else {
    beep();
  }
}

sub newline_noexec {
  LE::reset(1,1);
}

###############################################################################
# build the ref to names mapping
#
{
    my ($name,$glob);
    while (($name,$glob)=each %LE::) {
      local *tmp=$glob;
      $reftoname{\&tmp}=$name if (defined(&tmp));
    }
}

1;
