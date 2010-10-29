$loc=0;
$src="./";

sub new_require {
  my $arg=@_>0 ? $_[0] : $_;
  my ($pkg,$file,$line)=caller;
  if ($arg =~ /^\d+(\.\d+)?$/) {
    die "Perl $arg required--this is only version $], stopped at $file line $line.\n" if $arg>$];
    return 1;
  }
  $arg =~ s@::@/@sg;
  my $ext="";
  $ext=".pm" unless $arg =~ /\.p[ml]$/s;
  return 1 if ($INC{"$arg$ext"});
  for my $dir (@INC) {
    my $file = "$dir/$arg$ext";
    if (-r $file) {
      my $ret=do $file;
      die $@ if $@;
      die "$arg did not return a true value at $file line $line.\n" unless $ret;
      $INC{"$arg$ext"}=$file;
      print $arg,($loc==2 ? "$ext " : " ") if (($loc && -r "$src$arg$ext") || (!$loc && ! -r "$src$arg$ext"));
      return $ret;
    }
  }
  die "Can't locate $arg$ext in \@INC (\@INC contains: " . join(" ",@INC,'[BUILTIN]') . ") at $file line $line.\n";
}

BEGIN {
$::moddep_run=1;
*CORE::GLOBAL::require=\&new_require;
}
$loc = $ARGV[0] || 0;
$src = $ENV{srcdir} if $ENV{srcdir};
push(@INC,$src);
require Main;
print " Carp::Heavy Exporter::Heavy" if !$loc && $] >= 5.006;
print "\012";
