/* vim: ts=8
 * "The Road goes ever on and on, down from the door where it began."
 */

#include "window.h"
#include "socket.h"

#undef PACKAGE /* prevent #define conflicts */

#include "EXTERN.h"
#include "perl.h"

#ifndef PERL_SYS_INIT3
#define PERL_SYS_INIT3(x,y,z) PERL_SYS_INIT(x,y)
#endif

//#include "perlvars.h"

EXTERN_C void xs_init (pTHX);
static PerlInterpreter *my_perl;

#define	MAXARGV		64

#define	MakeStr(arg)  MakeStr2(arg)
#define MakeStr2(arg) #arg

const char  *iargv[]={"perl","-we",
"#line " MakeStr(__LINE__) " " MakeStr(__FILE__) "\n\n"
"sub new_require(*) {\n"
"  my $arg=@_>0 ? $_[0] : $_;\n"
"  my ($pkg,$cfile,$line)=caller;\n"
"  if ($arg =~ /^\\d+(\\.\\d+)?$/) {\n"
"    die \"Perl $arg required--this is only version $], stopped at $cfile line $line.\\n\" if $arg>$];\n"
"    return 1;\n"
"  }\n"
"  my @files; my $useinc;\n"
"  if ($arg =~ m=^[\\\\/]= || $arg =~ m=^\\.[\\\\/]= || $arg =~ m=^\\.\\.[\\\\/]=) {\n"
"    return 1 if $INC{$arg};\n"
"    push(@files,$arg)\n"
"  } else {\n"
"    $arg =~ s@::@/@g;\n"
"    $arg .= \".pm\" unless $arg =~ /\\.pm$/s;\n"
"    return 1 if $INC{\"$arg\"};\n"
"    @files=map { \"$_/$arg\" } @INC;\n"
"    $useinc=1;\n"
"  }\n"
"  for my $file (@files) {\n"
"    if (-r $file) {\n"
"      CL::i_msg($file eq $arg ? \"Loading $arg\" : \"Loading $arg from $file\"); CL::flush();\n"
"      my $ret=do $file;\n"
"      die $@ if $@;\n"
"      die \"$arg did not return a true value at $cfile line $line.\\n\" unless $ret;\n"
"      $INC{$useinc ? \"$arg\" : $arg}=$file;\n"
"      return $ret;\n"
"    }\n"
"  }\n"
"  $code=CL::get_module_code($arg);\n"
"  if (!$code) { my $tmp=$arg; $tmp =~ s/\\.pm$//s; $code=CL::get_module_code($tmp) }\n"
"  if ($code) {\n"
"    CL::i_msg(\"Loading builtin $arg\"); CL::flush();\n"
"    my $ret=eval \"#line 1 \\\"[BUILTIN]:$arg\\\"\\n$code\";\n"
"    die $@ if $@;\n"
"    die \"$arg did not return a true value at $cfile line $line.\\n\" unless $ret;\n"
"    $INC{\"$arg\"}=\"[BUILTIN]:$arg\";\n"
"    return $ret;\n"
"  }\n"
"  die \"Can't locate $arg in \\@INC (\\@INC contains: \" . join(\" \",@INC,'[BUILTIN]') . \") at $cfile line $line.\\n\" if $useinc;\n"
"  die \"Can't locate $arg at $cfile line $line.\\n\";\n"
"}\n"
"BEGIN {\n"
"  $CL::VERSION='0.01';\n"
"  $CL::VERSION='0.01';\n"
"  CL::bootstrap('CL');\n"
"  *CORE::GLOBAL::require=\\&new_require;\n"
"  push(@INC,$1) if $0 =~ m=^(..*)[/\\\\]=;\n"
"  $::rundir=$1 if $^O eq 'MSWin32' && $^X =~ m=^(.+)[\\\\/]=;\n"
"  push(@INC,$::rundir) if defined $::rundir;\n"
"  $SIG{__WARN__}=sub { die $_[0] unless $_[0] =~ /^Unquoted string / };\n"
"}\n"
"$|=1;\n"
"eval {\n"
"  require Main;\n"
"};\n"
"if ($@) {\n"
"  my $em=$@; $em=~tr/\\000-\\037/ /s;\n"
"  CL::err($em); # this should not be necessary due to our die handlers\n"
"} else {\n"
"  #delete $CORE::GLOBAL::{require};\n"
"  run();\n"
"}",
"--"};
#define	NARGV (sizeof(iargv)/sizeof(iargv[0]))

static int  sys_initialized=0;
static int  cleanup_installed=0;

static void cleanupall(void) {
  if (sys_initialized) {
    socket_cleanup();
    window_done();
    sys_initialized=0;
  }
}

static void initialized(void) {
  sys_initialized=1;
  if (!cleanup_installed) {
    cleanup_installed=1;
    atexit(cleanupall);
  }
}

#ifdef WIN32
#define	main  wmain
#endif

int main(int argc, char **argv,char **env)
{
    int exitstatus;

    char	*newargv[MAXARGV];
    size_t	ap=0;
    const char	*msg;

    if (argc>0) {
	newargv[ap++]=*argv++;
	--argc;
    }
    while (ap<NARGV) {
      newargv[ap]=(char*)iargv[ap];
      ++ap;
    }
    if ((size_t)argc>MAXARGV-ap-1)
	argc=MAXARGV-ap-1;

    while (argc--)
	newargv[ap++]=*argv++;

    newargv[ap]=NULL;

    argv=newargv;
    argc=ap;

    if ((msg=window_init())) {
#ifndef WIN32
      fprintf(stderr,"Can't initialize terminal: %s\n",msg);
#endif
      return 1;
    } else {
      int   i;
      for (i=0;i<10;++i)
	window_open();
      initialized();
    }

    PERL_SYS_INIT3(&argc,&argv,&env);
#if defined(USE_5005THREADS) || defined(USE_ITHREADS)
        PTHREAD_ATFORK(Perl_atfork_lock,
	               Perl_atfork_unlock,
	               Perl_atfork_unlock);
#endif

#if 0
    perl_init_i18nl10n(1);
#endif

    my_perl = perl_alloc();
    if (!my_perl)
	return 1;
    perl_construct( my_perl );
    PL_perl_destruct_level = 0;

    exitstatus = perl_parse( my_perl, xs_init, argc, argv, (char **) NULL );
    if (!exitstatus) {
      exitstatus = perl_run( my_perl );
    }

    perl_destruct( my_perl );
    perl_free( my_perl );

#ifndef WIN32
    PERL_SYS_TERM(); /* for some reason this references op mutex under win32 */
#endif

#ifndef WIN32
    /* flush our subsystems here */
    flush_socks();
    window_flush();
#endif

    return exitstatus;
}
