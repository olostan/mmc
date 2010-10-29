package Parser;

use strict;
use integer;
use locale;

use Conf;

# strange things happen... suddenly \G\z stoppped working

my $cmdre=qr/\Q$Conf::char\E/;

sub char(;$) {
    my $ch=shift;
    if ($ch) {
	$Conf::char=$ch;
	$cmdre=qr/\Q$Conf::char\E/;
    }
    $Conf::char;
}

sub subst_vars($;$) {
    no strict 'refs';
    local $^W=0; local $_=shift;
    s;\001([^\001]*)\001;($_=$1)=~y~0-9~~c?${"U::$_"}:$_[0]->[$_];ge;$_;
}

sub parse_commands($;$) {
    local $^W=0; # disable warnings, because we can concat undef sometimes
    local $_=shift;
    my $keep0=shift;
    my ($ps,$w,@wl,@cl,$nest,$fwf,$lws,$sv);

    pos=0;

S0: @wl=(1,0,0);
    $w=undef;

S3: m/\G\s*/sgc if $Conf::skipws;
    $sv=pos;
    goto S2 if (/\G([^{}${Conf::sep}\$\s\\]+)(\s+([^{}${Conf::sep}\$\s\\]+))?/osgc);
S5: if (/\G$cmdre(\d*):(:?)(\d+)\s*/gc) { $wl[0]=length($1) ? 0+$1 : 1; $wl[1]=0+$3; $wl[1]|=0x40000000 if length($2); goto S3; }
    if (/\G$cmdre(\d+)\s*/sgc) { $wl[0]=0+$1; goto S3; }
    if (/\G$cmdre/sgc) { $wl[2]=3; goto S1; }
    $lws=pos; $fwf=1;
    $wl[2]=1;

S4: if (/\G\{/sgc) { $ps='S4'; goto Q1; }
    goto E1 if (/\G[${Conf::sep}]/osgc);
    goto E2 if (pos==length);
    if (/\G\$/sgc) { $ps='S4'; goto V1; }
    if (/\G\\/sgc) { $ps='S4'; goto Q2; }
    if (/\G&/sgc) { $ps='S4'; goto C1; }
    $w.=$1 if (/\G([^${Conf::sep}{\\\$&]+)/osgc);
    goto S4;

S1: $lws=pos;
    m/\G([^${Conf::sep}{}\$&\s]*)/osgc;
    push(@wl,$1);
    $fwf=1;

A0: $fwf=0 if m/\G\s+/sgc;

A2: push(@wl,$w) if defined($w);
    $w=$keep0 && !$fwf ? '' : undef;
    $lws=pos if (!$fwf);
    $fwf=0;

A1: if (/\G\{/sgc) { $ps='A1'; goto Q1; }
    if (/\G\\/sgc) { $ps='A1'; goto Q2; }
    if (/\G\$/sgc) { $ps='A1'; goto V1; }
    if (/\G&/sgc) { $ps='A1'; goto C1; }
    goto E1 if (/\G[${Conf::sep}]/osgc);
    goto E2 if (pos==length);
    $w.=$1 if (/\G([^{${Conf::sep}\$\\\s&]+)/osgc);
    goto A2 if (/\G\s+/sgc);
    goto A1;

S2: if (defined(&{"U::$1"})) { $wl[2]=2; push(@wl,$1); pos()=$sv+length($1); goto A0; }
    if (defined(&{"U::$1 $3"})) { $wl[2]=2; push(@wl,"$1 $3"); goto A0; }
    pos=$sv;
    goto S5;

Q1: $nest=1; $w='' if (!defined($w));

Q3: if (/\G\}/sgc) { --$nest; goto $ps if !$nest; $w.='}'; goto Q3; }
    if (/\G\{/sgc) { ++$nest; $w.='{'; goto Q3; }
    goto E2 if (pos==length);
    $w.=$1 if (/\G([^{}]+)/sgc);
    goto Q3;

Q2: goto E2 if (pos==length);
    $w.=$1 if (/\G(.)/sgc);
    goto $ps;

C1: goto E2 if (pos==length);
    $w.="\003$1" if (/\G(.)/sgc);
    goto $ps;

V1: if (m/\G{([^}]*)}/sgc || m/\G(\d)/sgc || m/\G(\w+)/sgc) { $w.="\001$1\001" }
    goto $ps;

E1: $w='' if (!defined($w) && $wl[2]==1);
    push(@wl,$w) if (defined($w));
    push(@cl,[@wl]);
    goto S0;

E2: $w='' if (!defined($w) && $wl[2]==1);
    push(@wl,$w) if (defined($w));
    push(@cl,[@wl]);

    $keep0 ? ($lws,@cl) : @cl;
}

sub run_commands($;$) {
  my $cmd=shift;
  my $args=shift||[];
  $args->[0]=join(" ",@$args[1..$#$args]) if $#$args && !defined($args->[0]);
  for my $c (parse_commands($cmd)) {
    for (my $i=3;$i<=$#$c;$i++) {
      $c->[$i]=subst_vars($c->[$i],$args);
    }
    ::run_command($c);
  }
}

sub parse_string {
    local $_=shift;
    my $result='';
    my $nest;

S0: goto Q1 if (/\G\{/sgc);
    goto Q2 if (/\G\\/sgc);
    goto V1 if (/\G\$/sgc);
    goto C1 if (/\G&/sgc);
    goto E1 if (pos==length);
    $result.=$1 if (/\G([^{\\\$&]+)/sgc);
    goto S0;

Q1: $nest=1;

Q3: if (/\G\}/sgc) { --$nest; goto S0 if !$nest; $result.='}'; goto Q3; }
    if (/\G\{/sgc) { ++$nest; $result.='{'; goto Q3; }
    goto E1 if (pos==length);
    $result.=$1 if (/\G([^{}]+)/sgc);
    goto Q3;

Q2: goto E1 if (pos==length);
    $result.=$1 if (/\G(.)/sgc);
    goto S0;

V1: if (m/\G{([^}]*)}/sgc || m/\G(\d)/sgc || m/\G(\w+)/sgc) { $result.="\001$1\001" }
    goto S0;

C1: goto E1 if (pos==length);
    $result.="\003$1" if (/\G(.)/sgc);
    goto S0;

E1: $result;
}

sub format_string($) {
    my $result=shift;

    return '' if (!defined($result));
    $result =~ s/([ \$\&\\])/\\$1/g;
    $result =~ s/\003(.)/\&$1/g;
    $result =~ s/\001([^\001]*)\001(?![0-9A-Za-z_])/\$$1/sg; # convert variables 1
    $result =~ s/\001([^\001]*)\001/\${$1}/sg; # convert variables 2
    $result
}

sub format_qstring($) { # substs
  my $c=shift;
  $c =~ s/([\\\&\$])/\\$1/sg; # escape special chars
  $c =~ s/\003(.)/\&$1/sg; # convert colors
  $c =~ s/\001([^\001]*)\001(?![0-9A-Za-z_])/\$$1/sg; # convert variables 1
  $c =~ s/\001([^\001]*)\001/\${$1}/sg; # convert variables 2
  "{$c}" # quote it
}

1;
