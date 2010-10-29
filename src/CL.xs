#include "socket.h"
#include "window.h"
#include "timeout.h"
#include "misc.h"
#include "output.h"

#undef PACKAGE	/* prevet #define conflicts */

#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include <zlib.h>
#include <time.h>

/* #define	MY_EVAL	G_EVAL */
#define	MY_EVAL	0

/* the most complex part of this thing: callbacks */

static void timeout_callback(int cancel,SV *obj) {
  if (!cancel) {
    dSP;

    ENTER;
    SAVETMPS;

    PUSHMARK(SP);
    XPUSHs(obj);
    PUTBACK;

    perl_call_method("_run",G_DISCARD|G_VOID|MY_EVAL);

    FREETMPS;
    LEAVE;
  }
  SvREFCNT_dec(obj);
}

static void socket_callback(int sc_sock,int sc_code,SV *obj,
			    Char *sc_buf,int sc_buflen) {
    int	    freeobj=0;
    dSP;

    ENTER;
    SAVETMPS;

    PUSHMARK(SP);
    XPUSHs(obj);

    switch (sc_code) {
	case SCONN:
	    XPUSHs(sv_2mortal(newSViv(sc_sock)));
	    PUTBACK;
	    perl_call_method("connected",G_DISCARD|G_VOID|MY_EVAL);
	    break;
	case SEOF:
	    PUTBACK;
	    perl_call_method("remclosed",G_DISCARD|G_VOID|MY_EVAL);
	    freeobj=1;
	    break;
	case SLINE:
	    XPUSHs(sv_2mortal(newSVpvn((char*)sc_buf,sc_buflen*sizeof(Char))));
	    PUTBACK;
	    perl_call_method("line",G_DISCARD|G_VOID|MY_EVAL);
	    break;
	case SPROMPT:
	    XPUSHs(sv_2mortal(newSVpvn((char*)sc_buf,sc_buflen*sizeof(Char))));
	    PUTBACK;
	    perl_call_method("prompt",G_DISCARD|G_VOID|MY_EVAL);
	    break;
	case SECHOON:
	    XPUSHs(sv_2mortal(newSViv(1)));
	    PUTBACK;
	    perl_call_method("echo",G_DISCARD|G_VOID|MY_EVAL);
	    break;
	case SECHOOFF:
	    XPUSHs(sv_2mortal(newSViv(0)));
	    PUTBACK;
	    perl_call_method("echo",G_DISCARD|G_VOID|MY_EVAL);
	    break;
	case SIOERR:
	case SCLOSED:
	    PUTBACK;
	    perl_call_method("closed",G_DISCARD|G_VOID|MY_EVAL);
	    freeobj=1;
	    break;
	case SBELL:
	    PUTBACK;
	    perl_call_method("bell",G_DISCARD|G_VOID|MY_EVAL);
	    break;
    }

    FREETMPS;
    LEAVE;
    
    if (freeobj)
      SvREFCNT_dec(obj);
}

static SV	    *perltermcb=NULL;

static void	term_callback(int w,const char *key,int len,void *data) {
  if (!key) {
    switch (len) {
      case WMSG_ACT: {
	SV	*winfo=perl_get_sv("CL::winfo",TRUE|GV_ADDMULTI);
	sv_setiv(winfo,SvIV(winfo)|(1<<w));
	break;
      }
      default:
	break;
    }
  } else if (perltermcb) {
    dSP;

    ENTER;
    SAVETMPS;

    PUSHMARK(SP);
    XPUSHs(sv_2mortal(newSVpvn((char*)key,len)));
    PUTBACK;

    perl_call_sv(perltermcb,G_DISCARD|G_VOID|MY_EVAL);

    FREETMPS;
    LEAVE;
  }
}

SV  *get_clip(void) {
  SV	  *clip=NULL;
#ifdef WIN32
  HANDLE  hClip;
  char	  *cc;

  if (OpenClipboard(NULL)) {
    if (IsClipboardFormatAvailable(CF_TEXT) && (hClip=GetClipboardData(CF_TEXT))) {
      if ((cc=(char*)GlobalLock(hClip))) {
	clip=newSVpvn(cc,strlen(cc));
	GlobalUnlock(hClip);
      }
    }
    CloseClipboard();
  }
#else
#endif
  if (clip)
    return clip;
  return SvREFCNT_inc(&PL_sv_undef);
}

MODULE = CL	PACKAGE = CL	    

PROTOTYPES: ENABLE

char *
get_version()
  CODE:
    RETVAL = (char *)get_version();
  OUTPUT:
    RETVAL

# from lib.h

double
gettime()
    CODE:
	RETVAL = ((double)gettime())/1000.0;
    OUTPUT:
	RETVAL

double
gettime_ms()
    CODE:
	RETVAL = ((double)time(NULL))+((double)(gettime()%1000))/1000.0;
    OUTPUT:
	RETVAL

void
i_msg(fmt)
    char *  fmt
    CODE:
	clmsg(fmt);

void
warn(fmt)
    char *  fmt
    CODE:
	clwarnx(fmt);
	window_flush(); /* flush output after warnings and errors */

void
err(fmt)
    char *  fmt
    CODE:
	clerrx(fmt);
	window_flush();

void
gotowin(n)
  int n
  CODE:
    window_switch(n);

void
fetchline(w,n)
  int w
  int n
  PREINIT:
   const char *line;
   int	      len;
   int	      flag;
  PPCODE:
    if (window_fetchline(w,n,&line,&len,&flag)) {
      EXTEND(SP, 2);
      PUSHs(sv_2mortal(newSVpvn((char *)line,len*sizeof(Char))));
      PUSHs(sv_2mortal(newSViv(flag)));
    }

void
sbup(n)
  int n
  CODE:
    window_sbup(n);

void
sbdown(n)
  int n
  CODE:
    window_sbdown(n);

void
redraw()
  CODE:
    window_redraw();

void
flush()
  CODE:
    flush_socks();
    window_flush();

void
set_sb_context(lines)
    int	    lines
    CODE:
      window_set_confvar(WCF_SBSIZE,lines);

void
slowscroll(mode)
    int	    mode
    CODE:
      window_set_confvar(WCF_SLOWSCROLL,mode);

int
twidth()
    PREINIT:
	int	w,h,iw;
    CODE:
	window_getsize(&w,&h,&iw);
	RETVAL = w;
    OUTPUT:
	RETVAL

int
theight()
    PREINIT:
	int	w,h,iw;
    CODE:
	window_getsize(&w,&h,&iw);
	RETVAL = h;
    OUTPUT:
	RETVAL

int
curpos()
    CODE:
	RETVAL = window_input_pos();
    OUTPUT:
	RETVAL

void
statusconf(type,height)
  int type
  int height
  CODE:
    window_set_status_mode(type,height);

void
playsound(name)
    char *name;
    CODE:
	window_playsound(name);

void
tmoveto(newpos)
    int	newpos
    CODE:
      window_move_icursor(newpos);

void
tleft()
  CODE:
      window_move_icursor_left();

void
tright()
  CODE:
    window_move_icursor_right();

void
tdelc(x,n)
  int x
  int n
  CODE:
    window_delete_input(x,n);

void
tinsc(x,string,c)
  int x
  SV *string
  int c
  PREINIT:
    STRLEN    len;
    char      *sbuf;
  CODE:
    sbuf = SvPV(string,len);
    window_insert_input(x,len);
    window_output_input(x,sbuf,len,c);

void
tdeol()
  CODE:
    window_deol_input();

void
tnewline(w,string,flag=0)
    int	w
    SV	*string
    int flag
    PREINIT:
	STRLEN	len;
	char	*sbuf;
    CODE:
	sbuf = SvPV(string,len);
	window_output_ctext(w,flag,sbuf,len>>1);
	window_move_icursor(0);
	window_deol_input();

void
toutput(w,string,flag=0)
    int	w
    SV *string
    int flag
    PREINIT:
	STRLEN	len;
	char	*sbuf;
    CODE:
	sbuf = SvPV(string,len);
	window_output_ctext(w,flag,sbuf,len>>1);

void
twrite(x,string)
    int x
    SV *string
    PREINIT:
	STRLEN	len;
	char	*sbuf;
    CODE:
	sbuf = SvPV(string,len);
	window_output_cinput(x,sbuf,len>>1);

void
twritenc(x,string,c)
    int x
    SV *string
    int c
    PREINIT:
	STRLEN	len;
	char	*sbuf;
    CODE:
	sbuf = SvPV(string,len);
	window_output_input(x,sbuf,len,c);

void
twstatus(x,y,string,c)
    int x
    int y
    SV *string
    int c
    PREINIT:
	STRLEN	len;
	char	*sbuf;
    CODE:
	sbuf = SvPV(string,len);
	window_output_status(x,y,sbuf,len,c);

void
twcstatus(x,y,string)
    int x
    int y
    SV *string
    PREINIT:
	STRLEN	len;
	char	*sbuf;
    CODE:
	sbuf = SvPV(string,len);
	window_output_cstatus(x,y,sbuf,len>>1);

SV *
parse_colors(string,color=7)
    SV *string
    int color
    PREINIT:
	STRLEN	len;
	char	*sbuf;
	Char	dest[2048];
	int	destlen=sizeof(dest)/sizeof(dest[0]);
    CODE:
	sbuf = SvPV(string,len);
	parse_colors(sbuf,len,dest,&destlen,color);
	RETVAL = newSVpvn((char*)dest,destlen*sizeof(Char));
    OUTPUT:
	RETVAL

SV *
unparse_colors(string)
    SV	*string
    PREINIT:
	STRLEN	len;
	char	*sbuf;
	char	dest[2048];
	int	destlen=sizeof(dest);
    CODE:
	sbuf = SvPV(string,len);
	unparse_colors((Char*)sbuf,len/sizeof(Char),dest,&destlen);
	RETVAL = newSVpvn(dest,destlen);
    OUTPUT:
	RETVAL

SV *
strip_colors(string)
    SV	*string
    PREINIT:
	STRLEN	    srclen;
	char	    *src;
	char	    dest[2048];
	int	    destlen=sizeof(dest);
    CODE:
	src = SvPV(string,srclen);
	strip_colors((Char*)src,srclen/sizeof(Char),dest,&destlen);
	RETVAL = newSVpvn(dest,destlen);
    OUTPUT:
	RETVAL

void
print_message(message_type,message,error_message=NULL)
    int	    message_type
    char    *message
    char    *error_message
    CODE:
	print_message(message_type,error_message,message);

void
set_term_handler(cbproc)
    SV	*cbproc
    CODE:
	if (perltermcb)
	    SvSetSV(perltermcb,cbproc);
	else
	    perltermcb=newSVsv(cbproc);
	window_set_cb(term_callback,0);

void
set_vattr(statusbg)
	int		statusbg
	CODE:
	  window_set_confvar(WCF_STATUSBG,statusbg);

void
addkey(key,name)
  char *key
  char *name
  CODE:
    out_addkey(key,name);

int
main_loop_iteration()

void
post_quit_message()

int
loop_finished()

void
sclose(sock)
    int	sock

void
swrite(sock,string)
    int	sock
    SV *string
    PREINIT:
	STRLEN	len;
	char	*sbuf;
    CODE:
	sbuf = SvPV(string,len);
	swrite(sock,sbuf,len,0);

void
swriteln(sock)
    int	sock
    CODE:
	sputc(sock,'\n');

int
sconnect(addr,port,cbobj,doproc=1,laddr=NULL)
    char    *addr
    int	    port
    SV	    *cbobj
    char    *laddr
    int	    doproc
    CODE:
	SvREFCNT_inc(cbobj);
	RETVAL = sconnect(addr,port,laddr,doproc,(PSHANDLER)&socket_callback,cbobj);
	if (RETVAL<0)
	    SvREFCNT_dec(cbobj);
    OUTPUT:
	RETVAL

int
sconnect_pipe(cmd,cbobj,doproc=0)
  char	  *cmd
  SV	  *cbobj
  int	  doproc
  CODE:
    SvREFCNT_inc(cbobj);
    RETVAL = sconnect_pipe(cmd,doproc,(PSHANDLER)&socket_callback,cbobj);
    if (RETVAL<0)
      SvREFCNT_dec(cbobj);
  OUTPUT:
    RETVAL

void
sgetcounters(sock)
    int	    sock
    PREINIT:
    int	    rin,rout,pin;
    PPCODE:
	sgetcounters(sock,&rin,&rout,&pin);
	EXTEND(SP,3);
	PUSHs(sv_2mortal(newSViv(rin)));
	PUSHs(sv_2mortal(newSViv(rout)));
	PUSHs(sv_2mortal(newSViv(pin)));

void
set_lp_delay(newdelay)
  double  newdelay
  CODE:
    set_lp_delay((int)(newdelay*1000.0));

void
cancel_timeout(id)
    int	id

int
timeout(when,callback)
    double  when
    SV	    *callback
    CODE:
	SvREFCNT_inc(callback);
	RETVAL = stimeout((ltime_t)(when*1000.0),(PEVHANDLER)&timeout_callback,callback);
	if (RETVAL<0)
	    SvREFCNT_dec(callback);
    OUTPUT:
	RETVAL


# builtin modules handler
SV *
get_module_code(modname)
    char    *modname
    PREINIT:
      int     i,osize,psize;
      const char    *name,*pdata;
    CODE:
      RETVAL=NULL;
      for (i=0;get_packed_module_data(i,&name,&pdata,&osize,&psize);i++)
	if (!strcmp(name,modname)) {
	  uLongf  destlen;
	  char	*tmp;
	  New(0,tmp,osize,char);
	  if (!tmp)
	    break;
	  destlen=osize;
	  if (uncompress((Bytef*)tmp,&destlen,(const Bytef *)pdata,psize)!=Z_OK) {
	    Safefree(tmp);
	    break;
	  }
	  RETVAL = newSVpvn(tmp,osize);
	  Safefree(tmp);
	  break;
	}
      if (!RETVAL)
	RETVAL = SvREFCNT_inc(&PL_sv_undef);
    OUTPUT:
      RETVAL

# clipboard support
SV *
get_clipboard()
  CODE:
    RETVAL=get_clip();
  OUTPUT:
    RETVAL

# fetch socket info
SV *
get_tcp_info(i,j)
  int	i
  int	j
PREINIT:
  char	  tmp[1024];
  int	  size;
CODE:
  RETVAL=NULL;
  size=sizeof(tmp);
  if (sock_get_tcp_info(i,j,tmp,&size)) {
    RETVAL = newSVpvn(tmp,size);
  } else
    RETVAL = SvREFCNT_inc(&PL_sv_undef);
OUTPUT:
  RETVAL

#set per-socket lp
void
sock_setlp(s,lp)
  int s
  int lp
CODE:
  sosetlp(s,lp);

