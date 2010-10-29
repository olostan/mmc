#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#define	read  _read
#define	write _write
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/telnet.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <fcntl.h>

#if !defined(INADDR_NONE)
#define	INADDR_NONE 0xffffffff
#endif
#endif

#include <zlib.h>

#include "config.h"
#include "socket.h"
#include "timeout.h"
#include "output.h"
#include "window.h"
#include "misc.h"

#ifndef HAVE_SOCKLEN_T
typedef int socklen_t;
#endif

#define	clisprint(foo)	(((unsigned char)(foo))>=32)

#define	NSOCK	16

static ltime_t  lpdelay=250;

void  set_lp_delay(int newdelay) {
  lpdelay=newdelay;
}

#ifndef WIN32
typedef int SOCKET;
#else
typedef int	socklen_t;
#endif

#define	SM_NORM	  0
#define	SM_CONN	  1
#define	SM_HALF	  2

#define	RT_SOCK	  0
#define	RT_PIPE	  1

#define	KERN_SOCKBUFSIZE  65536
#define PIPE_BUFSIZE	  8192

static struct socket {
    int		    inuse;
    SOCKET	    sock;
#ifdef WIN32
    HANDLE	    event;
#endif

#define	IBSIZE	    4096
    Char	    ibuf[IBSIZE];
    size_t	    ilen;
    unsigned char   obuf[1024];
    size_t	    olen;

    z_streamp	    zsp;

    size_t	    rawin;
    size_t	    procin;
    size_t	    rawout;

    int		    lp;
    int		    lpevent;
    int		    state;
    unsigned short  attr;
    int		    args[10];
    int		    acnt;
    unsigned char   tcmd;

    unsigned char   sbbuf[128];
    size_t	    sbbptr;

    int		    mode;
    int		    opmode;
    int		    type;

    struct sockaddr_in	remote;

    PSHANDLER	    handler;
    void	    *data;
} sockets[NSOCK];

#define	sNORMAL	    0
#define	sIAC	    1
#define	sDW	    2
#define	sESC	    3
#define	sGNUM	    4
#define	sSUBNEG	    5
#define	sSUBNEGD    6

#ifdef WIN32

#define	IAC	255
#define	DONT	254
#define	DO	253
#define	WONT	252
#define	WILL	251
#define	SB	250
#define	GA	249
#define	EOR	239
#define	SE	240

#define	TELOPT_ECHO 1

static WSADATA	wsadata;
static int	initdone=0;

void	socket_cleanup(void) {
    int	    i;

    if (!initdone)
	return;
    for (i=0;i<NSOCK;i++)
	if (sockets[i].inuse)
	    closesocket(sockets[i].sock);
    WSACleanup();
    initdone=0;
}

static int  initsockets(void) {
    if (!initdone) {
	if (WSAStartup(MAKEWORD(2,0),&wsadata)) {
	    clwarn("failed to initialize winsock2 library");
	    return -1;
	}
	initdone=1;
    }
    return 0;
}

static const char   *sockerror(void) {
    int	    code,size;
    static char	errbuf[512];

    if (!initdone)
	return NULL;
    code=WSAGetLastError();
    if (!code)
      code=GetLastError();
    size=FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS|
	FORMAT_MESSAGE_MAX_WIDTH_MASK,NULL,code,0,errbuf,sizeof(errbuf)-1,NULL);
    if (size) {
      errbuf[size]='\0';
      return errbuf;
    }
    return NULL;
}

#define	dnserror()  sockerror()

#else

static const char   *sockerror(void) {
    return strerror(errno);
}

static const char   *dnserror(void) {
  return hstrerror(h_errno);
}

#define	closesocket(s)	close(s)

#endif

static void cancel_lpe(int s) {
    if (sockets[s].lpevent>=0) {
	cancel_timeout(sockets[s].lpevent);
	sockets[s].lpevent=-1;
    }
}

static void sockerrmsg(const char *m,int dns) {
    char	buf[2048];
    const char	*se;
    size_t	i;

    i=strlen(m);
    if (i>sizeof(buf)-3)
      return;
    strncpy(buf,m,i);
    buf[i]='\0';
    strcat(buf,": ");
    if ((se=dns ? dnserror() : sockerror()))
	if (strlen(se)+i<sizeof(buf))
	  strcat(buf,se);
    clwarnx(buf);
}

int	sconnect_pipe(char *cmd,int doproc,PSHANDLER hf,void *ha) {
    int			idx;

    if (!hf)
	return -1;
    for (idx=0;idx<NSOCK;idx++)
	if (!sockets[idx].inuse)
	    break;
    if (idx==NSOCK) {
	clwarnx("too many open sockets");
	return -1;
    }
    sockets[idx].handler=hf;
    sockets[idx].data=ha;
    sockets[idx].ilen=sockets[idx].olen=0;
    sockets[idx].lp=1;
    sockets[idx].lpevent=-1;
    sockets[idx].state=sNORMAL;
    sockets[idx].attr=7;
    sockets[idx].mode=sockets[idx].opmode=doproc ? SM_NORM : SM_HALF;
    sockets[idx].type=RT_PIPE;
    sockets[idx].zsp=NULL;
    sockets[idx].rawin=sockets[idx].rawout=sockets[idx].procin=0;
#ifdef WIN32
    if (!StartExtCmd(cmd,idx))
      return -1;
    sockets[idx].sock=-1;
    sockets[idx].event=NULL;
#else
    {
      int   pfd[2],pid,i,maxfd;
      char  *shell;
      if (socketpair(PF_LOCAL,SOCK_STREAM,0,pfd)<0)
	return -1;
      sockets[idx].sock=pfd[0];
      if ((pid=fork())<0) {
	close(pfd[0]);
	close(pfd[1]);
	return -1;
      }
      if (pid==0) {
	// do the standard double fork trick to avoid handling zombies
	if (fork())
	  _exit(0);
	setsid();
	close(pfd[0]);
	if (pfd[1]!=0)
	  dup2(pfd[1],0);
	if (pfd[1]!=1)
	  dup2(pfd[1],1);
	if (pfd[1]!=2)
	  dup2(pfd[1],2);
	if (pfd[1]!=1 && pfd[1]!=2 && pfd[1]!=0)
	  close(pfd[1]);
	close(0);
	maxfd=getdtablesize();
	for (i=3;i<maxfd;++i)
	  close(i);
	shell=getenv("SHELL");
	if (!shell)
	  shell="/bin/sh";
	execl(shell,shell,"-c",cmd,NULL);
	_exit(127);
      }
      waitpid(pid,&i,0);
    }
#endif
    sockets[idx].inuse=1;
    if (sockets[idx].handler)
      sockets[idx].handler(idx,SCONN,sockets[idx].data,NULL,0);
    return idx;
}

int	sconnect(char *addr,int port,char *laddr,int doproc,PSHANDLER hf,void *ha) {
    int			idx,opt;
    struct sockaddr_in	rem,loc;

#define FAIL0(m) { clwarnx(m); return -1; }
#define	FAIL1(m) { sockerrmsg(m,0); return -1; }
#define	FAIL2(m) { sockerrmsg(m,0); goto free_sock; }
#define	FAIL3(m) { sockerrmsg(m,0); goto free_event; }
#define	FAILD(m) { sockerrmsg(m,1); return -1; }

#ifdef WIN32
    if (initsockets()<0)
	return -1;
#endif

    if (!hf)
	return -1;
    for (idx=0;idx<NSOCK;idx++)
	if (!sockets[idx].inuse)
	    break;
    if (idx==NSOCK) {
	clwarnx("too many open sockets");
	return -1;
    }
    sockets[idx].handler=hf;
    sockets[idx].data=ha;
    sockets[idx].ilen=sockets[idx].olen=0;
    sockets[idx].lp=1;
    sockets[idx].lpevent=-1;
    sockets[idx].state=sNORMAL;
    sockets[idx].attr=7;
    sockets[idx].mode=SM_NORM;
    sockets[idx].opmode=doproc ? SM_NORM : SM_HALF;
    sockets[idx].type=RT_SOCK;
    sockets[idx].zsp=NULL;
    sockets[idx].rawin=sockets[idx].rawout=sockets[idx].procin=0;
    window_flush(); /* flush output before a long blocking operation */
    memset(&rem,0,sizeof(rem));
    rem.sin_family=AF_INET;
    rem.sin_port=htons((short)port);
    if ((rem.sin_addr.s_addr=inet_addr(addr))==INADDR_NONE) {
	struct hostent	*hp=gethostbyname(addr);

	if (!hp)
	    FAILD("can't get remote address");
	memcpy(&rem.sin_addr,hp->h_addr,sizeof(rem.sin_addr));
    }
    memcpy(&sockets[idx].remote,&rem,sizeof(sockets[idx].remote));
    if (laddr) {
	int   slen;
	char  *cp,*buf;

	memset(&loc,0,sizeof(loc));
	loc.sin_family=AF_INET;
	slen=strlen(laddr);
	for (cp=laddr+slen;cp>laddr;--cp)
	  if (*cp==':')
	    break;
	if (*cp==':') {
	  buf=malloc(cp-laddr+1);
	  if (!buf)
	    FAIL0("out of memory");
	  memcpy(buf,laddr,cp-laddr);
	  buf[cp-laddr]='\0';
	  ++cp;
	  loc.sin_port=htons((short)atoi(cp));
	} else
	  buf=laddr;
	if ((loc.sin_addr.s_addr=inet_addr(buf))==INADDR_NONE) {
	    struct hostent  *hp=gethostbyname(buf);

	    if (!hp)
		FAILD("can't get local address");
	    memcpy(&loc.sin_addr,hp->h_addr,sizeof(loc.sin_addr));
	}
	if (buf!=laddr)
	  free(buf);
    }
    if ((sockets[idx].sock=socket(PF_INET,SOCK_STREAM,0))<0)
	FAIL1("can't create socket");
    if (laddr)
	if (bind(sockets[idx].sock,(struct sockaddr *)&loc,sizeof(loc))<0)
	    FAIL2("can't bind socket");
#ifdef WIN32
    if ((sockets[idx].event=CreateEvent(NULL,FALSE,FALSE,NULL))==NULL) {
	clwarn("can't create event");
	goto free_sock;
    }
    if (WSAEventSelect(sockets[idx].sock,sockets[idx].event,FD_READ|FD_CLOSE|FD_CONNECT))
	FAIL3("can't bind event to socket");
#endif
#ifndef WIN32
    {
      int flags=fcntl(sockets[idx].sock,F_GETFL,0);
      if (flags<0)
	FAIL3("can't get fd flags");
      if (fcntl(sockets[idx].sock,F_SETFL,flags|O_NONBLOCK)<0)
	FAIL3("can't set fd flags");
    }
#endif
    opt=KERN_SOCKBUFSIZE;
    setsockopt(sockets[idx].sock,SOL_SOCKET,SO_RCVBUF,(void*)&opt,sizeof(opt)); /* ignore result */
    if (connect(sockets[idx].sock,(struct sockaddr *)&rem,sizeof(rem))<0) {
#ifdef WIN32
      if (WSAGetLastError()!=WSAEWOULDBLOCK)
#else
      if (errno!=EINPROGRESS)
#endif
	FAIL3("can't connect to remote host");
      sockets[idx].mode=SM_CONN;
      sockets[idx].inuse=1;
    } else {
      sockets[idx].inuse=1;
      if (sockets[idx].handler)
	sockets[idx].handler(idx,SCONN,sockets[idx].data,NULL,0);
    }
    return idx;
free_event:
#ifdef WIN32
    CloseHandle(sockets[idx].event);
#endif
free_sock:
    closesocket(sockets[idx].sock);
    return -1;
#undef FAIL0
#undef FAIL1
#undef FAIL2
#undef FAIL3
}

void	sgetcounters(int s,int *rin,int *rout,int *pin) {
  *rin=*rout=*pin=0;
  if (s<0 || s>=NSOCK || !sockets[s].inuse)
    return;
  *rin=sockets[s].rawin;
  *rout=sockets[s].rawout;
  *pin=sockets[s].procin;
}

static void iclose(int s) {
    cancel_lpe(s);
#ifdef WIN32
    if (sockets[s].type==RT_SOCK) {
      closesocket(sockets[s].sock);
      CloseHandle(sockets[s].event);
    }
#else
    closesocket(sockets[s].sock);
#endif
    if (sockets[s].zsp) {
      inflateEnd(sockets[s].zsp);
      free(sockets[s].zsp);
      sockets[s].zsp=NULL;
    }
    sockets[s].inuse=2;
}

static int  iread(int s,void *ptr,size_t count) {
    int	    result;

    if (s<0 || s>=NSOCK || !sockets[s].inuse)
	return -1;
    if (!count)
	return 0;
    if (sockets[s].type==RT_SOCK)
      result=recv(sockets[s].sock,ptr,count,0);
    else
#ifdef WIN32
    {
      DWORD   nr;
      if (ReadFile((HANDLE)sockets[s].sock,ptr,count,&nr,NULL))
	result=nr;
      else
	result=-1;
    }
#else
      result=read(sockets[s].sock,ptr,count);
#endif
    if (result<=0)
	iclose(s);
    sockets[s].rawin+=result;
    return result;
}

static int  iwrite(int s,void *ptr,size_t count) {
    int	    result;

    if (s<0 || s>=NSOCK || !sockets[s].inuse)
	return -1;
    if (!count)
	return 0;
    if (sockets[s].type==RT_SOCK)
      result=send(sockets[s].sock,ptr,count,0);
    else
#ifdef WIN32
    {
      DWORD nw;
      if (WriteFile((HANDLE)sockets[s].sock,ptr,count,&nw,NULL))
	result=nw;
      else
	result=-1;
    }
#else
      result=write(sockets[s].sock,ptr,count);
#endif
    if (result<=0)
	iclose(s);
    sockets[s].rawout+=result;
    return result;
}

static void start_mccp(int s) {
  z_stream  *zp;

  if ((zp=(z_stream*)malloc(sizeof(z_stream)))==NULL) {
    clerrx("Out of memory!");
    sockets[s].handler(s,SIOERR,sockets[s].data,NULL,0);
    iclose(s);
    return;
  }
  memset(zp,0,sizeof(*zp));
  if (inflateInit(zp)!=Z_OK) {
    clerrx("zlib initialization error");
    iclose(s);
    free(zp);
    return;
  }
  sockets[s].zsp=zp;
  clmsg("Starting MCCP.");
}

static void lpe_handler(int cancel,struct socket *s) {
    if (cancel)
      return;
    if (s->inuse && s->lp && s->ilen) {
	s->handler(s-sockets,SPROMPT,s->data,s->ibuf,s->ilen);
	s->ilen=0;
    }
    s->lpevent=-1;
}

#define ADDC(ch) { if (s->ilen<IBSIZE && clisprint(ch)) s->ibuf[s->ilen++]=MKCH(ch,s->attr); }

static size_t  process_half(int sock,const unsigned char *sbuf,size_t slen) {
  const unsigned char	*p,*e;
  unsigned char		c;
  struct socket		*s=sockets+sock;

  p=sbuf; e=sbuf+slen;

  while (p<e)
    switch (c=*p++) {
      case '\r':
	break;
      case '\n':
	s->handler(sock,SLINE,s->data,s->ibuf,s->ilen);
	s->ilen=0;
	break;
      default:
	ADDC(c);
	break;
    }
  s->procin+=slen;
  return 0;
}

static size_t process_sockbuf(int sock,const unsigned char *sbuf,size_t slen) {
    int			i,newattr;
    const unsigned char	*p,*e;
    unsigned char	c;
    struct socket	*s=sockets+sock;

    if (s->mode==SM_HALF)
      return process_half(sock,sbuf,slen);

    p=sbuf; e=sbuf+slen;

    while (p<e)
	switch (s->state) {
	    case sNORMAL:
		switch (c=*p++) {
		    case '\a':
			s->handler(sock,SBELL,s->data,NULL,0);
			break;
		    case '\t':
			i=8-s->ilen%8;
			while (i--)
			    ADDC(' ');
			break;
		    case '\n':
			s->handler(sock,SLINE,s->data,s->ibuf,s->ilen);
			s->ilen=0;
			break;
		    case '\033':
			s->state=sESC;
			break;
		    case IAC:
			s->state=sIAC;
			break;
		    default:
			ADDC(c);
			break;
		}
		break;
	    case sIAC:
		switch (c=*p++) {
		    case DO:
		    case DONT:
		    case WILL:
		    case WONT:
			s->tcmd=c;
			s->state=sDW;
			break;
		    case GA:
		    case EOR:
			s->lp=0;
			cancel_lpe(sock);
			s->handler(sock,SPROMPT,s->data,s->ibuf,s->ilen);
			s->ilen=0;
			s->state=sNORMAL;
			break;
		    case SB: /* subneg */
			s->sbbptr=0;
			s->state=sSUBNEG;
			break;
		    case IAC:
			ADDC(IAC);
			s->state=sNORMAL;
			break;
		    default:
			s->state=sNORMAL;
			break;
		}
		break;
	    case sSUBNEG:
		switch (c=*p++) {
		  case IAC:
		    s->state=sSUBNEGD;
		    break;
		  case SE:
		    /*	this certainly sucks, mccp authors didnt read telnet
			rfc apparently */
		    if (s->sbbptr==2 && s->sbbuf[0]==85 && s->sbbuf[1]==WILL)
		      goto mccp_braindamage;
		  default:
		    if (s->sbbptr<sizeof(s->sbbuf))
		      s->sbbuf[s->sbbptr++]=c;
		    break;
		}
		break;
	    case sSUBNEGD:
		switch (c=*p++) {
		  case IAC:
		    if (s->sbbptr<sizeof(s->sbbuf))
		      s->sbbuf[s->sbbptr++]=IAC;
		    s->state=sSUBNEG;
		    break;
		  case SE:
mccp_braindamage:   s->state=sNORMAL;
		    if ((s->sbbptr==2 && s->sbbuf[0]==85 && s->sbbuf[1]==WILL) ||
			(s->sbbptr==1 && s->sbbuf[0]==86))
		    {
		      if (!s->zsp) {
			start_mccp(sock);
			if (p<e) {
			  s->procin+=p-sbuf;
			  return e-p; /*  return immediately because the mccp was
					  started */
			}
		      }
		    }
		    break;
		  default:
		    s->state=sNORMAL;
		}
		break;
	    case sDW:
		switch (c=*p++) {
		    case TELOPT_ECHO:
			if (s->tcmd==WILL)
			    s->handler(sock,SECHOOFF,s->data,NULL,0);
			if (s->tcmd==WONT)
			    s->handler(sock,SECHOON,s->data,NULL,0);
			break;
		    case 85: case 86: /* mccp, always allow compression */
			if (s->tcmd==WILL) {
			  unsigned char	tmp[]={IAC,DO,0};
			  tmp[2]=c;
			  swrite(sock,tmp,3,1);
			  clmsg("Enabling MCCP.");
			}
			break;
		}
		s->state=sNORMAL;
		break;
	    case sESC:
		switch (*p++) {
		    case '[':
			s->acnt=0;
			s->args[0]=0;
			s->state=sGNUM;
			break;
		    default:
			s->state=sNORMAL;
			break;
		}
		break;
	    case sGNUM:
		switch (c=*p++) {
		    case '\n':
			s->handler(sock,SLINE,s->data,s->ibuf,s->ilen);
			s->ilen=0;
			s->state=sNORMAL;
			break;
		    case IAC:
			s->state=sIAC;
			break;
		    case '0': case '1': case '2': case '3': case '4': case '5':
		    case '6': case '7': case '8': case '9':
			s->args[s->acnt]=s->args[s->acnt]*10+(int)(c-'0');
			break;
		    case ';':
			if (s->acnt<9)
			    s->acnt++;
			s->args[s->acnt]=0;
			break;
		    case 'm':
			newattr=s->attr;
			for (i=0;i<=s->acnt;i++)
			    if (s->args[i]==0)
				newattr=7;
			    else if (s->args[i]==1)
				newattr|=8;
			    else if (s->args[i]>=30&&s->args[i]<=37)
				newattr=(newattr&8)|((s->args[i]-30)&7);
			s->attr=newattr;
			s->state=sNORMAL;
			break;
		    default:
			s->state=sNORMAL;
			break;
		}
		break;
	    default:
		s->state=sNORMAL;
	}
    cancel_lpe(sock);
    if (s->ilen>0 && s->lp && s->state==sNORMAL) {
      if (lpdelay>0)
	s->lpevent=stimeout(gettime()+lpdelay,(PEVHANDLER)&lpe_handler,s);
      else {
	s->handler(s-sockets,SPROMPT,s->data,s->ibuf,s->ilen);
	s->ilen=0;
      }
    }
    s->procin+=slen;
    return 0;
}

#define FAIL(x) { sockerrmsg(x,0); goto er; }
static void sofinish(int s) {
  int	    opt;
  socklen_t optlen;

  optlen=sizeof(opt);
  if (getsockopt(sockets[s].sock,SOL_SOCKET,SO_ERROR,(char*)&opt,&optlen)<0)
    FAIL("getsockopt failed");
  if (opt) {
#ifdef WIN32
    WSASetLastError(opt);
#else
    errno=opt;
#endif
    FAIL("can't connect to remote host");
  }
#if 0
  /*  this code was stolen from some other tcp/ip app, looks like this is
      needed for some ancient SunOS, should do no harm on other OSes */
  if (connect(sockets[s].sock,(struct sockaddr *)&sockets[s].remote,sizeof(sockets[s].remote))<0 &&
	    errno!=EISCONN) {
    read(sockets[s].sock,&opt,1);
    FAIL("can't connect to remote host");
  }
#endif
  if (sockets[s].handler)
    sockets[s].handler(s,SCONN,sockets[s].data,NULL,0);
  sockets[s].mode=sockets[s].opmode;
  return;
er:
  if (sockets[s].handler)
    sockets[s].handler(s,SIOERR,sockets[s].data,NULL,0);
  iclose(s);
}
#undef FAIL

void soproc(int s,void *extbuf,int extlen) {
    unsigned char   rdbuf[16384],*ptr=rdbuf;
    int		    rdlen;
    int		    remain;

    if (sockets[s].mode==SM_CONN) {
      sofinish(s);
      if (sockets[s].inuse==2)
	sockets[s].inuse=0;
      return;
    }
    if (extbuf) {
      ptr=extbuf;
      rdlen=extlen;
      if (extlen<=0)
	iclose(s);
    } else
      rdlen=iread(s,rdbuf,sizeof(rdbuf));
    if (rdlen<0) {
	sockerrmsg("read from socket failed",0);
	sockets[s].handler(s,SIOERR,sockets[s].data,NULL,0);
    } else if (rdlen==0) {
	sockets[s].handler(s,SEOF,sockets[s].data,NULL,0);
    } else { /* got data, do mccp stuff now */
#ifdef LOGALLINPUT
    write(3,rdbuf,rdlen);
#endif
      while (rdlen>0) {
	remain=0;
	if (sockets[s].zsp) {
	  z_stream	*zp=sockets[s].zsp;
	  unsigned char	tmp[8192];
	  int		res;

	  zp->next_in=ptr;
	  zp->avail_in=rdlen;
	  while (zp->avail_in>0) {
	    zp->next_out=tmp;
	    zp->avail_out=sizeof(tmp);
	    res=inflate(zp,Z_SYNC_FLUSH);
	    if (res==Z_OK) {
	      if (zp->avail_out!=sizeof(tmp))
		remain=process_sockbuf(s,tmp,sizeof(tmp)-zp->avail_out);
	    } else if (res==Z_NEED_DICT) {
	      clerrx("Oops, zlib wants a dictionary, where can i find that, sir?");
	      sockets[s].handler(s,SIOERR,sockets[s].data,NULL,0);
	      iclose(s);
	      break;
	    } else if (res==Z_STREAM_END) {
	      if (zp->avail_out!=sizeof(tmp))
		remain=process_sockbuf(s,tmp,sizeof(tmp)-zp->avail_out);
	      remain=zp->avail_in;
	      inflateEnd(zp);
	      free(zp);
	      sockets[s].zsp=NULL;
	      clmsg("Stopping MCCP.");
	      break;
	    } else {
	      strcpy((char *)tmp,"zlib: ");
	      if (zp->msg)
		strncat((char *)tmp,zp->msg,sizeof(tmp));
	      else
		strncat((char *)tmp,"unknown error.",sizeof(tmp));
	      tmp[sizeof(tmp)-1]='\0';
	      clerrx((char *)tmp);
	      sockets[s].handler(s,SIOERR,sockets[s].data,NULL,0);
	      iclose(s);
	      break;
	    }
	  }
	} else
	  remain=process_sockbuf(s,ptr,(size_t)rdlen);
	ptr+=rdlen-remain;
	rdlen=remain;
      }
    }
    if (sockets[s].inuse==2)
      sockets[s].inuse=0;
}

static int  soflush(int s) {
    unsigned char *optr=sockets[s].obuf;
    size_t	  l=sockets[s].olen;
    int		  n;

    while (l>0) {
	n=iwrite(s,optr,l);
	if (n<0) {
	    sockerrmsg("write to socket failed",0);
	    sockets[s].handler(s,SIOERR,sockets[s].data,NULL,0);
	    if (sockets[s].inuse==2)
	      sockets[s].inuse=0;
	    return -1;
	}
	optr+=n;
	l-=n;
    }
    sockets[s].olen=0;
    return 0;
}

void	swrite(int s,const void *ptr,int count,int raw) {
    const unsigned char *optr=(const unsigned char*)ptr;
    if (s<0 || s>=NSOCK || !sockets[s].inuse)
	return;

    while (count>0) {
	if (sockets[s].olen>=sizeof(sockets[s].obuf))
	    if (soflush(s)<0)
		return;
	if (raw) {
	  while (count>0 && sockets[s].olen<sizeof(sockets[s].obuf)) {
	    sockets[s].obuf[sockets[s].olen++]=*optr;
	    count--;
	    optr++;
	  }
	} else {
	  while (count>0 && sockets[s].olen<sizeof(sockets[s].obuf)) {
	      if (*optr==0xff) {
		  sockets[s].obuf[sockets[s].olen++]=0xff;
		  if (sockets[s].olen>=sizeof(sockets[s].obuf))
		      if (soflush(s)<0)
			  return;
		  sockets[s].obuf[sockets[s].olen++]=0xff;
	      } else
		  sockets[s].obuf[sockets[s].olen++]=*optr;
	      count--;
	      optr++;
	  }
	}
    }
}

void	sputc(int s,char c) {
    swrite(s,&c,1,0);
}

void flush_socks(void) {
    int	    i;

    for (i=0;i<NSOCK;i++)
	if (sockets[i].inuse && sockets[i].olen>0)
	    soflush(i);
}

void	sclose(int s) {
    if (s<0 || s>=NSOCK || !sockets[s].inuse)
	return;
    if (soflush(s)<0)
	return;
    sockets[s].handler(s,SCLOSED,sockets[s].data,NULL,0);
    iclose(s);
    if (sockets[s].inuse==2)
      sockets[s].inuse=0;
}

void	sosetlp(int s,int lp) {
  if (s<0 || s>=NSOCK || !sockets[s].inuse)
    return;
  sockets[s].lp=!!lp;
}

static int  time_has_come=0;

void	post_quit_message(void) {
    time_has_come=1;
}

int	loop_finished(void) {
  return time_has_come;
}

#ifdef WIN32
int	main_loop_iteration(void) {
    HANDLE  hlist[NSOCK];
    int	    i,j;
    DWORD   r,delay;
    ltime_t tm;

    flush_socks();
    window_flush();
    for (i=0,j=0;j<NSOCK;j++)
	if (sockets[j].inuse && sockets[j].event)
	    hlist[i++]=sockets[j].event;
    tm=nearest_timeout();
    if (tm<0)
	delay=INFINITE;
    else if (tm<0x7fffffff)
	delay=(DWORD)tm;
    else
	delay=0x7fffffff;
    r=MsgWaitForMultipleObjects(i,hlist,FALSE,delay,QS_ALLINPUT);
    if (r<0) {
	clerr("MsgWaitForMultipleObjectsEx() failed");
	return -1;
    }
    if (r==WAIT_OBJECT_0+i)
	out_process_input();
    else if (r>=WAIT_OBJECT_0 && r<WAIT_OBJECT_0+i) {
	r-=WAIT_OBJECT_0;
	for (j=0;j<NSOCK;j++)
	    if (sockets[j].event==hlist[r])
		soproc(j,NULL,0);
    }
    process_timeouts();
    return 0;
}

int sock_get_tcp_info(int s,int full,char *buf,int *bufsize) {
  return 0;
}
#else

#include <sys/wait.h>
#include <signal.h>

static void	sigchld(int dummy) {
  int	  status;
  while (waitpid(-1,&status,WNOHANG)>0) ;
}

static void	fatalsig(int sig) {
  time_has_come=1;
}

int	main_loop_iteration(void) {
    fd_set	    rfd,wfd;
    struct timeval  delay;
    int		    i,j,r;
    ltime_t	    tm;
    static int	    siginit=0;

    if (!siginit) {
      signal(SIGCHLD,sigchld);
      signal(SIGINT,fatalsig);
      signal(SIGTERM,fatalsig);
      signal(SIGHUP,fatalsig);
      signal(SIGQUIT,fatalsig);
      siginit=1;
    }
    flush_socks();
    window_flush();
    FD_ZERO(&rfd);
    FD_ZERO(&wfd);
    FD_SET(0,&rfd);
    for (j=i=0;j<NSOCK;j++)
	if (sockets[j].inuse) {
	    if (i<sockets[j].sock)
	      i=sockets[j].sock;
	    FD_SET(sockets[j].sock,&rfd);
	    if (sockets[j].mode==SM_CONN)
	      FD_SET(sockets[j].sock,&wfd);
	}
    tm=nearest_timeout();
    if (tm<0)
	r=select(i+1,&rfd,&wfd,NULL,NULL);
    else {
	delay.tv_usec=(tm%1000)*1000;
	delay.tv_sec=tm/1000;
	r=select(i+1,&rfd,&wfd,NULL,&delay);
    }
    if (r<0) {
	if (errno!=EINTR) {
	    clerr("select() failed");
	    return -1;
	}
    }
    out_sigcheck(); /* just in case that was SIGWINCH */
    if (r>0) {
	if (FD_ISSET(0,&rfd))
	    out_process_input();
	for (j=0;j<NSOCK;j++)
	    if (sockets[j].inuse && (FD_ISSET(sockets[j].sock,&rfd) || FD_ISSET(sockets[j].sock,&wfd)))
		    soproc(j,NULL,0);
    }
    process_timeouts();
    return 0;
}

#ifdef __FreeBSD__
#include <netinet/tcp.h>
#include <netinet/tcp_var.h>
#include <sys/sysctl.h>

#define	TCP_GETCB   0xff

#endif

int sock_get_tcp_info(int s,int full,char *buf,int *bufsize) {
#ifdef __FreeBSD__
  static int HZ;

  struct tcpcb	tp;
  int		len;

  if (!HZ) {
    struct clockinfo ci;
    int		     len=sizeof(ci);
    if (sysctlbyname("kern.clockrate",&ci,&len,NULL,0)<0 || len!=sizeof(ci))
      HZ=100; /* fallback */
    else
      HZ=ci.hz;
  }
  if (s<0 || s>=NSOCK || !sockets[s].inuse)
      return 0;
  len=sizeof(tp);
  if (getsockopt(sockets[s].sock,IPPROTO_TCP,TCP_GETCB,&tp,&len)<0 ||
      len!=sizeof(tp))
    return 0;
  if (full)
    len=snprintf(buf,*bufsize,
      "state=%x flags=%x snd_una=%u snd_nxt=%u snd_max=%u rcv_wnd=%d "
      "snd_wnd=%d snd_cwnd=%d rcvtime=%d rtttime=%d rxtcur=%d "
      "srtt=%d rttvar=%d rxtshift=%d",
      tp.t_state, tp.t_flags, tp.snd_una, tp.snd_nxt, tp.snd_max, tp.rcv_wnd,
      tp.snd_wnd, tp.snd_cwnd, tp.t_rcvtime, tp.t_rtttime, tp.t_rxtcur,
      tp.t_srtt, tp.t_rttvar, tp.t_rxtshift);
  else
    len=snprintf(buf,*bufsize,"%u %d %d",tp.snd_nxt-tp.snd_una,
	(tp.t_srtt*1000/HZ)>>TCP_RTT_SHIFT,
	(tp.t_rttvar*1000/HZ)>>TCP_RTTVAR_SHIFT);
  if (len<0)
    return 0;
  *bufsize=len;
  return 1;
#else
  return 0;
#endif
}

#endif

