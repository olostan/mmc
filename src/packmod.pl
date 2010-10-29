sub mfind {
	my $mod=shift;
	for my $p (@INC) {
		return "$p/$mod" if (-r "$p/$mod");
	}
	return undef;
}

push(@INC,$ENV{srcdir}) if $ENV{srcdir};
select(STDOUT);
$|=1;

print <<EOF
#include <stdlib.h>
#include "misc.h"

static struct {
	int			origsize;
	int			packedsize;
	const char	*name;
	const char	*moddata;
} perlmodules[]={
EOF
;
for my $m (@ARGV) {
	my $mod=$m;
	$mod =~ s/::/\//g;
	my $ext="";
	$ext=".pm" unless $mod =~ /\.p[ml]$/s;
	my $f;
	if ($mod =~ /^(.*)=(.*)$/) {
		$mod=$1;
		$f=$2;
		die "$f does not exist" unless -r $f;
	} else {
		$f=mfind($mod . $ext);
		die "Can't find $mod in \@INC\n" if (!$f);
	}
	system("./b2c $f $mod");
	die "b2c failed: $?\n" if ($?);
}

print <<EOF
};

#define	NMOD	(sizeof(perlmodules)/sizeof(perlmodules[0]))

int		get_packed_module_data(int idx,const char **name,const char **pdata,
								int *osize,int *psize) {
	if (idx<0 || (size_t)idx>=NMOD)
		return 0;
	*name=perlmodules[idx].name;
	*pdata=perlmodules[idx].moddata;
	*osize=perlmodules[idx].origsize;
	*psize=perlmodules[idx].packedsize;
	return 1;
}
EOF
;
