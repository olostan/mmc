package Status;

use strict;
use fields qw(x y width rwidth align color contents);
use CL;

my $statuscolor=4;
my @svars;
my $ready=0;

sub setcolors($) {
  $statuscolor=$_[0];
}

sub format_single_status_line($) {
  my $sline=shift;
  my $w=CL::twidth();
  my $x=0;
  my $i;

  $sline->{delim}=undef;
  for ($i=0;$i<=$#{$sline->{vars}};++$i) {
    $sline->{vars}[$i]{y}=$sline->{y};
    if ($sline->{vars}[$i]{width}==0) { # delimiter between right and left parts
      $sline->{delim}=$i;
      $sline->{vars}[$i]{x}=$x;
    } else {
      $sline->{vars}[$i]{rwidth}=$sline->{vars}[$i]{width};
      $sline->{vars}[$i]{rwidth}=$w if $sline->{vars}[$i]{rwidth}>$w;
      $sline->{vars}[$i]{x}=$x;
      $w-=$sline->{vars}[$i]{rwidth};
      $x+=$sline->{vars}[$i]{rwidth};
    }
  }
  if (defined($sline->{delim})) { # adjust delimiter width
    my $space=CL::twidth()-$sline->{vars}[$sline->{delim}]{x};
    for (my $j=$sline->{delim}+1;$j<=$#{$sline->{vars}};++$j) {
      $space-=$sline->{vars}[$j]{rwidth};
    }
    $sline->{vars}[$sline->{delim}]{rwidth}=$space;
    for (my $j=$sline->{delim}+1;$j<=$#{$sline->{vars}};++$j) {
      $sline->{vars}[$j]{x}+=$space;
    }
  }
}

sub pad($$$) {
  my $text=shift;
  my $w=shift;
  my $align=shift;
  $text=substr($text,0,$w<<1);
  if ($align) { # sucks, must be left padded
    " \007" x ($w-(length($text)>>1)) . $text;
  } else { # cool, left padded
    $text . " \007" x ($w-(length($text)>>1));
  }
}

sub refresh_line($) {
  my $l=shift;
  return unless defined $l;
  format_single_status_line($l);
  my $c='';
  my $v;
  for $v (@{$l->{vars}}) {
    $c .= pad(CL::parse_colors($v->{contents},$v->{color}),$v->{rwidth},$v->{align}) if $v->{rwidth}>0;
    }
  $c .= " \007" x (CL::twidth()-(length($c)>>1));
  CL::twcstatus(0,$l->{y},$c);
}

sub refresh() {
  refresh_line($_) for @svars;
}

sub TIESCALAR {
  no strict 'refs';
  my $class=shift;
  my $y=shift;
  my $width=shift;
  my $color=shift;
  my $val=shift;
  my $self = { };
  if ($width<0) {
    $self->{align}=1;
    $self->{width}=-$width;
  } else {
    $self->{align}=0;
    $self->{width}=$width;
  }
  $self->{contents}=$val;
  $self->{color}=$color;
  $self->{y}=$y;
  push(@{$svars[$y]{vars}},$self);
  $svars[$y]{y}=$y;
  bless \$self,$class; # scalar hack
}

sub FETCH {
  ${$_[0]}->{contents};
}

sub STORE {
  my $self=${$_[0]};
  my $oldc=$self->{contents};
  $self->{contents}=$_[1];
  CL::twcstatus($self->{x},$self->{y},pad(CL::parse_colors($self->{contents},$self->{color}),$self->{rwidth},$self->{align})) if $oldc ne $self->{contents} && $ready && $self->{rwidth}>0;
}

sub DESTROY {
  return unless defined($_[0]) && ref($_[0]) eq "Status"; # need this to avoid problems during global destruction
  my $self=${$_[0]};
  for (my $i=0;$i<=$#{$svars[$self->{y}]{vars}};++$i) {
    if ($svars[$self->{y}]{vars}[$i] eq $self) {
      splice(@{$svars[$self->{y}]{vars}},$i,1);
      refresh_line($svars[$self->{y}]);
      return;
    }
  }
}

my @constants;

sub new_sv($;$$) {
  my $y=0;
  my $w=$_[1]||length($_[0]);
  my $c=$_[2];
  my $val=$_[0]||'';
  $c=$Conf::statusfg if (!defined($c));
  eval {
    tie $_[0],'Status',$y,$w,$c,$val;
  };
  if ($@) {
    if ($@ =~ /^Modification of a read-only value attempted/) {
      my $tmp=$_[0];
      tie $tmp, 'Status', $y, $w, $c, $val;
      push(@constants,\$tmp);
    } else {
      die $@;
    }
  }
  refresh_line($svars[$y]) if $ready;
}

sub new_svy($$;$$) {
  my $y=$_[1];
  my $w=$_[2]||length($_[0]);
  my $c=$_[3];
  my $val=$_[0]||'';
  $c=$Conf::statusfg if (!defined($c));
  eval {
    tie $_[0],'Status',$y,$w,$c,$val;
  };
  if ($@) {
    if ($@ =~ /^Modification of a read-only value attempted/) {
      my $tmp=$_[0];
      tie $tmp, 'Status', $y, $w, $c, $val;
      push(@constants,\$tmp);
    } else {
      die $@;
    }
  }
  refresh_line($svars[$y]) if $ready;
}

sub go() {
  $ready=1;
  refresh();
}

END {
  undef(@svars);
}

1;
