AUTOMAKE_OPTIONS = foreign no-dependencies
bin_PROGRAMS = mmc
noinst_PROGRAMS = b2c
mmc_SOURCES = socket.c timeout.c window.c misc.c output.c cmalloc.c
mmc_LDADD = main.o CL.o version.o perlmodules.o xsinit.o @LDFLAGS@ @PLDFLAGS@
BUILT_SOURCES = perlmodules.c CL.c xsinit.c
CLEANFILES = perlmodules.c CL.c xsinit.c
b2c_sources = b2c.c
EXTRA_DIST = @PRIVMODDEP@ moddep.pl packmod.pl socket.h misc.h cmalloc.h window.h output.h window_priv.h timeout.h main.c CL.xs version.c Char.h DLWin32.pm README Makefile.win32 config_h.win32 win32.c mmc.rc resource.h

perlmodules.c: @PRIVMODDEP@ $(srcdir)/packmod.pl b2c
	@PERL@ -I $(srcdir) $(srcdir)/packmod.pl @SYSMOD@ @PRIVMOD@ > perlmodules.c.tmp
	mv -f perlmodules.c.tmp perlmodules.c

CL.c: $(srcdir)/CL.xs
	@PERL@ @PRIVLIB@/ExtUtils/xsubpp -typemap @PRIVLIB@/ExtUtils/typemap $(srcdir)/CL.xs > CL.c.tmp
	mv -f CL.c.tmp CL.c

CL.o: CL.c
	$(COMPILE) -c CL.c

xsinit.c:
	@PERL@ -MExtUtils::Embed -e xsinit -- -std -o xsinit.c CL

xsinit.o: xsinit.c
	$(COMPILE) -c xsinit.c

main.o: $(srcdir)/main.c
	$(COMPILE) -c $(srcdir)/main.c

version.o: $(srcdir)/version.c
	$(COMPILE) -DVERSION=\"@VERSION@\" -c $(srcdir)/version.c

# XXX hack hack hack
dist-hook:
	-@PERL@ -pi -e 's/\.(\d\d\d\d)/sprintf(".%04d",$$1+1)/e' $(srcdir)/version.c
	-cp $(srcdir)/version.c $(distdir)/version.c
