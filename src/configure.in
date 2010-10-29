dnl Process this file with autoconf to produce a configure script.
AC_INIT(version.c)
AM_INIT_AUTOMAKE(mmc, 4.1)
AM_CONFIG_HEADER(config.h)

AC_SUBST(PRIVLIB)
AC_SUBST(PERL)
AC_SUBST(PLDFLAGS)
AC_SUBST(PRIVMOD)
AC_SUBST(PRIVMODDEP)
AC_SUBST(SYSMOD)

AC_CHECK_PROG(PERL,perl,perl)
if test "X$ac_cv_prog_PERL" = "X"; then
  AC_MSG_ERROR(perl not found)
fi
AC_MSG_CHECKING(C compiler)
[ CC="`$PERL -MConfig -e 'print $Config{cc}'`" ]
AC_MSG_RESULT($CC)
AC_MSG_CHECKING(C compiler flags)
[ CFLAGS="`$PERL -MExtUtils::Embed -e ccopts -- -std` `$PERL -MConfig -e 'print $Config{optimize}'` $CFLAGS" ]
AC_MSG_RESULT($CFLAGS)
AC_MSG_CHECKING(linker)
[ LD="`$PERL -MConfig -e 'print $Config{ld}'`" ]
AC_MSG_RESULT($LD)
AC_MSG_CHECKING(linker flags)
[ PLDFLAGS="`$PERL -MExtUtils::Embed -e ldopts -- -std`" ]
AC_MSG_RESULT($PLDFLAGS)
AC_PROG_CC
AC_MSG_CHECKING(module dependencies)
[ PRIVMODDEP="`$PERL -I $srcdir $srcdir/moddep.pl 2`" ]
AC_MSG_RESULT($PRIVMODDEP)
AC_MSG_CHECKING(modules)
[ PRIVMOD="`$PERL -I $srcdir $srcdir/moddep.pl 1`" ]
AC_MSG_RESULT($PRIVMOD)
AC_MSG_CHECKING(system modules)
[ SYSMOD="`$PERL -I $srcdir $srcdir/moddep.pl 0`" ]
AC_MSG_RESULT($SYSMOD)
AC_MSG_CHECKING(perl library dir)
[ PRIVLIB="`$PERL -MConfig -e 'print $Config{privlibexp}'`" ]
AC_MSG_RESULT($PRIVLIB)

AC_CHECK_LIB(ncurses, setupterm,,AC_CHECK_LIB(curses, setupterm,,AC_MSG_ERROR(curses not found)))

AC_CHECK_FUNC(socket,, AC_CHECK_LIB(socket, socket,,))
AC_CHECK_FUNC(gethostbyname,, AC_CHECK_LIB(nsl, gethostbyname,,))
AC_CHECK_FUNC(snprintf,AC_DEFINE(HAVE_SNPRINTF),)
AC_CHECK_LIB(z, compress,[LIBS="-lz $LIBS"],AC_MSG_ERROR(cant find libz))
AC_CHECK_HEADERS(stdint.h)
if test "X$ac_cv_header_stdint_h" = "Xno"; then
  AC_CHECK_TYPE(uint16_t,unsigned short)
fi

# check if the system has socklen_t
AC_MSG_CHECKING(socklen_t)
AC_TRY_COMPILE(
#include <sys/types.h>
#include <sys/socket.h>
,socklen_t i;,
[
    AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_SOCKLEN_T)
],
  AC_MSG_RESULT(no))

AC_OUTPUT(Makefile)
