#include "output.h"
#include "window_priv.h"
#include "cmalloc.h"

#ifdef WIN32
#include "config_h.win32"
#else
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/ioctl.h>
#ifdef HAVE_LIBNCURSES
#include <ncurses.h>
#else
#include <curses.h>
#include <term.h>
#endif
#include <termios.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>

#ifdef sun
typedef char  putchar_type;
#else
typedef int   putchar_type;
#endif

static void   keyinit(void);

static int    cursorx=-1,cursory=-1,cant_write_to_last_column=0;
static int    width=1,height=1;

/* output with counters */
static int    esc_chars,norm_chars;

static int    winputchar(putchar_type c) {
  ++esc_chars;
  return putc(c,stdout);
}

static int    winprintf(const char *fmt,...) {
  char	  buf[128];
  va_list ap;
  size_t  n;

  va_start(ap,fmt);
  n=vfprintf(stdout,fmt,ap);
  va_end(ap);
  esc_chars+=n;
  return n;
}

static void   xadjust(void) { /* XXX */
  if (cursorx==width) {
    if (cant_write_to_last_column) { /* wrap */
      cursorx=0;
      if (cursory<height-1)
	++cursory;
    } else { /* this is really tricky, we don't wrap, but can't write anymore */
      /* do nothing for now :( */
    }
  }
}

/* colors */
static int    cur_fg=100;
static int    cur_bg=100;
static int    want_fg=100,want_bg=100;
static void   (*set_attrs)(void);
static char   *t_rev,*t_bold,*t_sgr0,*t_smul,*t_rmul;
static char   *t_setaf,*t_setab;

/* xterm is weird and does not always apply new [background?] color until a
   printable character is output using the new colors */
static void color_setattrs(void) {
  /* using setaf, setab & bold, bg -1 is mapped to 0 */
  if (want_fg<0 || want_fg>15 || want_bg>7)
    return;
  if (want_fg>7 && (!t_bold || !t_sgr0))
    want_fg&=~8;
  if (!t_setab)
    want_bg=0;
  if (want_fg==cur_fg && want_bg==cur_bg)
    return;
  if (want_bg<0)
    want_bg=0;
  if (want_fg!=cur_fg && !(want_fg&8) && (cur_fg&8)) { /* have to reset bold */
    tputs(t_sgr0,1,winputchar);
#if 0
    if (want_bg!=0)
#endif
      tputs(tparm(t_setab,want_bg),1,winputchar);
#if 0
    if (want_fg!=7)
#endif
      tputs(tparm(t_setaf,want_fg),1,winputchar);
  } else {
    if (want_bg!=cur_bg)
      tputs(tparm(t_setab,want_bg),1,winputchar);
    if (want_fg!=cur_fg) {
      if ((want_fg&8) && !(cur_fg&8))
	tputs(t_bold,1,winputchar);
      if ((want_fg&7) != (cur_fg&7))
	tputs(tparm(t_setaf,want_fg&7),1,winputchar);
    }
  }
}

static void mono_setattrs(void) {
  /* mapping fg 1-7 normal, 0,8 - underline, 9-15 bold */
  /*         bg -1,0 - normal, 1-7 reverse */
  if (want_fg==0 || want_fg==8)
    want_fg=1;
  else if (want_fg>=1 && want_fg<=7)
    want_fg=0;
  else if (want_fg>=9 && want_fg<=15)
    want_fg=2;
  else
    return;
  if (want_bg<=0)
    want_bg=0;
  else if (want_bg>=1 && want_bg<=7)
    want_bg=1;
  else
    return;
  if (want_fg==2 && (!t_bold || !t_sgr0))
    want_fg=0;
  else if (want_fg==1 && !t_smul)
    want_fg=0;
  if (want_bg==1 && (!t_rev || !t_sgr0))
    want_bg=0;
  if (want_fg==cur_fg && want_bg==cur_bg)
    return;
  if ((want_bg!=cur_bg && cur_bg) ||
      (want_fg!=cur_fg && cur_fg==2) ||
      (!t_rmul && want_fg!=cur_fg && cur_fg==1)) {
    tputs(t_sgr0,1,winputchar);
    if (want_bg==1)
      tputs(t_rev,1,winputchar);
    if (want_fg==2)
      tputs(t_bold,1,winputchar);
    else if (want_fg==1)
      tputs(t_smul,1,winputchar);
  } else {
    if (want_bg!=cur_bg)
      tputs(t_rev,1,winputchar);
    if (want_fg==0 && cur_fg==1)
      tputs(t_rmul,1,winputchar);
    else if (want_fg==1 && cur_fg==0)
      tputs(t_smul,1,winputchar);
    else if (want_fg==2 && cur_fg==0)
      tputs(t_bold,1,winputchar);
    else if (want_fg==2 && cur_fg==1) {
      tputs(t_rmul,1,winputchar);
      tputs(t_bold,1,winputchar);
    }
  }
}

static void color_xterm_setattrs(void) {
  if (want_fg==cur_fg && want_bg==cur_bg)
    return;
  if (want_fg<0 || want_fg>15 || want_bg>7)
    return;
  if (want_bg!=cur_bg && want_bg<0) { /* want a default bg */
    if (want_fg&8)
      winprintf("\033[0;3%c;1m",want_fg+'0'-8);
    else
      winprintf("\033[0;3%cm",want_fg+'0');
  } else {
    if (want_fg!=cur_fg) {
      if ((want_fg^cur_fg)&8) { /* bold changed */
	if (want_fg&8) { /* good */
	  if (want_bg!=cur_bg)
	    winprintf("\033[3%c;4%c;1m",want_fg+'0'-8,want_bg+'0');
	  else
	    winprintf("\033[3%c;1m",want_fg+'0'-8);
	} else { /* bad */
#if 0
	  if (want_fg==7) {
	    if (want_bg<0)
	      winprintf("\033[m");
	    else
	      winprintf("\033[0;4%cm",want_bg+'0');
	  } else {
#endif
	    if (want_bg<0) { /* default bg, ok */
	      winprintf("\033[0;3%cm",want_fg+'0');
	    } else { /* bg is not default */
	      winprintf("\033[0;3%c;4%cm",want_fg+'0',want_bg+'0');
	    }
#if 0
	  }
#endif
	}
      } else { /* everything is cool */
	if (want_bg!=cur_bg)
	  winprintf("\033[3%c;4%cm",(want_fg&7)+'0',want_bg+'0');
	else
	  winprintf("\033[3%cm",(want_fg&7)+'0');
      }
    } else if (want_bg!=cur_bg)
      winprintf("\033[4%cm",want_bg+'0');
  }
}

static void   dosetcolor(void) {
  if (set_attrs)
    set_attrs();
  cur_bg=want_bg;
  cur_fg=want_fg;
}

void   out_setcolor(int fg,int bg) {
  if (fg>=0)
    want_fg=fg;
  want_bg=bg;
}

void   out_cwrite(const char *ctext,int len,int bg) {
  int	  i;

  if (cant_write_to_last_column && cursory==height-1 && cursorx+len>=width)
    --len;
  norm_chars+=len;
  cursorx+=len;
  xadjust();
  for (i=0;i<2*len;i+=2) {
    out_setcolor(ctext[i+1]&15,bg);
    if ((unsigned char)ctext[i]<0x20) {
      dosetcolor();
      putc('_',stdout);
    } else if (ctext[i]==' ') {
      if (want_bg!=cur_bg)
	dosetcolor();
      putc(' ',stdout);
    } else {
      dosetcolor();
      putc(ctext[i],stdout);
    }
  }
}

void   out_rawrite(const char *text,int len) {
  int	  i;

  dosetcolor();
  if (cant_write_to_last_column && cursory==height-1 && cursorx+len>=width)
    --len;
  norm_chars+=len;
  cursorx+=len;
  xadjust();
  for (i=0;i<len;++i) {
    if ((unsigned char)text[i]<0x20)
      putc('_',stdout);
    else
      putc(text[i],stdout);
  }
}

/* terminal capabilities */
static char   *t_ich,*t_ich1;
static char   *t_dch,*t_dch1;
static char   *t_smir,*t_rmir;
static char   *t_il,*t_il1;
static char   *t_dl,*t_dl1;
static char   *t_csr;
static char   *t_cup;
static char   *t_clear;
static int    tf_clear;
static char   *t_clearline;
static char   *t_smkx,*t_rmkx;
static char   *t_smcup,*t_rmcup;
static char   *t_smam,*t_rmam;
static char   *t_sc,*t_rc;

void   out_inschars(int n) {
  dosetcolor(); /* for bce terminals */
  if (t_ich && (!t_ich1 || n>1))
    tputs(tparm(t_ich,n),1,winputchar);
  else if (t_ich1) {
    while (n--)
      tputs(t_ich1,1,winputchar);
  } else {
    tputs(t_smir,1,winputchar);
    while (n--)
      putc(' ',stdout);
    tputs(t_rmir,1,winputchar);
  }
}

void   out_delchars(int n) {
  dosetcolor(); /* for bce terminals */
  if (t_dch && (!t_dch1 || n>1))
    tputs(tparm(t_dch,n),1,winputchar);
  else
    while (n--)
      tputs(t_dch1,1,winputchar);
}

static void   out_inslines(int n) {
  dosetcolor();
  if (t_il && (!t_il1 || n>1))
    tputs(tparm(t_il,n),n,winputchar);
  else
    while (n--)
      tputs(t_il1,1,winputchar);
}

static void   out_dellines(int n) {
  if (t_dl && (!t_dl1 || n>1))
    tputs(tparm(t_dl,n),n,winputchar);
  else
    while (n--)
      tputs(t_dl1,1,winputchar);
}

static int    wintop,winbot;

void   out_setscroll(int top,int bot) {
  wintop=top;
  winbot=bot;
  if (t_csr) {
    tputs(tparm(t_csr,top,bot),1,winputchar);
    cursorx=-1; cursory=-1;
  }
}

void   out_movecursor(int x,int y) {
  if (x==cursorx && y==cursory)
    return;
  if (cursory==y && x==0)
    putc('\r',stdout);
  else if (cursory==y && x==cursorx-1)
    putc('\b',stdout);
  else
    tputs(tparm(t_cup,y,x),1,winputchar);
  cursorx=x; cursory=y;
}

void   out_clearscr(void) {
  if (!tf_clear)
    out_movecursor(0,0);
  out_setcolor(7,-1);
  dosetcolor();
  tputs(t_clear,1,winputchar);
  cursorx=cursory=0;
}

void   out_clearline(void) {
  dosetcolor();
  tputs(t_clearline,1,winputchar);
}

static int    savecursorx,savecursory;

static void   out_savecursor(void) {
  savecursorx=cursorx;
  savecursory=cursory;
  if (t_sc && t_rc)
    tputs(t_sc,1,winputchar);
}

static void   out_restorecursor(void) {
  if (t_sc && t_rc) {
    tputs(t_rc,1,winputchar);
    cursorx=savecursorx;
    cursory=savecursory;
  } else
    out_movecursor(savecursorx,savecursory);
}

static char   *getcap(const char *cap) {
  char	*ret;

  ret=tigetstr((char*)cap);
  if (!ret || ret==(char *)-1)
    return NULL;
  return chk_strdup(ret);
}

static int resized=0;

static void sigwinch(int sig) {
  resized=1;
}

void	out_update_winsize(void) {
  struct winsize    sz;

  if (ioctl(0,TIOCGWINSZ,&sz)<0)
    sz.ws_col=sz.ws_row=0;
  if (sz.ws_col==0)
    sz.ws_col=80;
  if (sz.ws_row==0)
    sz.ws_row=24;
  width=sz.ws_col;
  height=sz.ws_row;
  window_resize(width,height);
}

void	out_sigcheck(void) {
  if (resized) {
    resized=0;
    out_update_winsize();
  }
}

void   out_drawlines(void *w,int count,int bg) {
  int	    i;
  const char	    *text;
  int	    len;

  out_setcolor(-1,bg);
  if (t_csr) { /* ahh, super, will scroll scroll scroll */
    out_movecursor(0,winbot);
    for (i=count-1;i>=0;--i) {
      dosetcolor();
      putc('\n',stdout);
      if (window_getline((struct Window *)w,i,&text,&len)>=0) {
	out_cwrite(text,len,bg);
	putc('\r',stdout);
      }
    }
    norm_chars+=count<<1;
  } else {
    /* optimization for these terminals, might produce ugly visual
       results */
    if (count>winbot-wintop+1)
      count=winbot-wintop+1;
    out_movecursor(0,wintop);
    out_dellines(count);
    out_movecursor(0,winbot+1-count);
    out_inslines(count);
    for (i=count-1;i>=0;--i) {
      len=0;
      if (window_getline((struct Window *)w,i,&text,&len)>=0)
	out_cwrite(text,len,bg);
      if (i>0 && len<width) {
	winputchar('\r');
	winputchar('\n');
      }
    }
  }
}

/* terminal input handlers */
static struct keydef {
  const char	*name;
  const char	*val;
  const	char	*cap;
} tkeys[]={
/* hardcoded defaults */
  /* function keys */
  { "f1",   "\033[11~",	NULL	},
  { "f1",   "\033[OP",	NULL	},
  { "f2",   "\033[12~",	NULL	},
  { "f2",   "\033[OQ",	NULL	},
  { "f3",   "\033[13~",	NULL	},
  { "f3",   "\033[OR",	NULL	},
  { "f4",   "\033[14~",	NULL	},
  { "f4",   "\033[OS",	NULL	},
  { "f11",  "\033[23~",	NULL	},
  { "f12",  "\033[24~",	NULL	},
  { "f13",  "\033[25~",	NULL	},
  { "f14",  "\033[26~",	NULL	},
  { "f15",  "\033[28~",	NULL	},
  { "f16",  "\033[29~",	NULL	},
  { "f17",  "\033[31~",	NULL	},
  { "f18",  "\033[32~",	NULL	},
  { "f19",  "\033[33~",	NULL	},
  { "f20",  "\033[34~",	NULL	},
  /* arrow keys */
  { "up",   "\033[A",	NULL	},
  { "up",   "\033OA",	NULL	},
  { "down", "\033[B",	NULL	},
  { "down", "\033OB",	NULL	},
  { "left", "\033[D",	NULL	},
  { "left", "\033OD",	NULL	},
  { "right","\033[C",	NULL	},
  { "right","\033OC",	NULL	},
  /* editing keys */
  { "ins",  "\033[2~",	NULL	},
  { "del",  "\177",	NULL	},
  { "del",  "\033[3~",	NULL	},
  { "home", "\033[7~",	NULL	},
  { "home", "\033[H",	NULL	},
  { "home", "\033[1~",	NULL	},
  { "end",  "\033[F",	NULL	},
  { "end",  "\033[8~",	NULL	},
  { "end",  "\033[4~",	NULL	},
  { "bs",   "\010",	NULL	},
  { "pgup", "\033[5~",	NULL	},
  { "pgdn", "\033[6~",	NULL	},
  /* keypad keys */
  { "k/",   "\033Oo",	NULL	},
  { "k*",   "\033Oj",	NULL	},
  { "k-",   "\033Om",	NULL	},
  { "k+",   "\033Ok",	NULL	},
  { "k+",   "\033Ol",	NULL	},
  { "C-M",  "\033OM",	NULL	},
  /* control keys */
  { "C-@",   "",	NULL	},
  { "C-A",   "\001",	NULL	},
  { "C-B",   "\002",	NULL	},
  { "C-C",   "\003",	NULL	},
  { "C-D",   "\004",	NULL	},
  { "C-E",   "\005",	NULL	},
  { "C-F",   "\006",	NULL	},
  { "C-G",   "\007",	NULL	},
  { "C-I",   "\011",	NULL	},
  { "C-M",   "\012",	NULL	},
  { "C-K",   "\013",	NULL	},
  { "C-L",   "\014",	NULL	},
  { "C-M",   "\015",	NULL	},
  { "C-N",   "\016",	NULL	},
  { "C-O",   "\017",	NULL	},
  { "C-P",   "\020",	NULL	},
  { "C-Q",   "\021",	NULL	},
  { "C-R",   "\022",	NULL	},
  { "C-S",   "\023",	NULL	},
  { "C-T",   "\024",	NULL	},
  { "C-U",   "\025",	NULL	},
  { "C-V",   "\026",	NULL	},
  { "C-W",   "\027",	NULL	},
  { "C-X",   "\030",	NULL	},
  { "C-Y",   "\031",	NULL	},
  { "C-Z",   "\032",	NULL	},
  { "C-\\",  "\034",	NULL	},
  { "C-]",   "\035",	NULL	},
  { "C-^",   "\036",	NULL	},
  { "C-_",   "\037",	NULL	},
/* terminfo keys */
  /* function keys */
  { "f1",   NULL,	"kf1"	},
  { "f2",   NULL,	"kf2"	},
  { "f3",   NULL,	"kf3"	},
  { "f4",   NULL,	"kf4"	},
  { "f5",   NULL,	"kf5"	},
  { "f6",   NULL,	"kf6"	},
  { "f7",   NULL,	"kf7"	},
  { "f8",   NULL,	"kf8"	},
  { "f9",   NULL,	"kf9"	},
  { "f10",  NULL,	"kf10"	},
  { "f11",  NULL,	"kf11"	},
  { "f12",  NULL,	"kf12"	},
  /* arrow keys */
  { "up",   NULL,	"kcuu1"	},
  { "down", NULL,	"kcud1"	},
  { "left", NULL,	"kcub1"	},
  { "right",NULL,	"kcuf1"	},
  /* editing keys */
  { "ins",  NULL,	"kich1"	},
  { "del",  NULL,	"kdch1"	},
  { "home", NULL,	"khome"	},
  { "end",  NULL,	"kend"	},
  { "bs",   NULL,	"kbs"	},
  { "pgup", NULL,	"kpp"	},
  { "pgdn", NULL,	"knp"	},
  /* keypad keys, some of these are nonstandard */
  { "C-M",  NULL,	"kent"	},
};

#define	NTKEYS	  (sizeof(tkeys)/sizeof(tkeys[0]))

struct keyparse {
  struct keyparse	*next;
  const char		*prefix;
  size_t		preflen;
  struct keydef		*key;
};

#define	HLEN	223

struct keyparse	  *keyhash[HLEN];

static struct keyparse	*hfind(const char *key,size_t len,int add) {
  unsigned int	    hv=0;
  size_t	    i;
  struct keyparse   *kp;

  for (i=0;i<len;++i)
    hv=hv*33+key[i];
  hv+=hv>>5;
  hv%=HLEN;
  for (kp=keyhash[hv];kp;kp=kp->next)
    if (len==kp->preflen && memcmp(key,kp->prefix,(size_t)len)==0)
      return kp;
  if (add) {
    kp=chk_malloc(sizeof(*kp));
    kp->prefix=key;
    kp->preflen=len;
    kp->next=keyhash[hv];
    keyhash[hv]=kp;
    return kp;
  }
  return NULL;
}

static void addkey(struct keydef *k) {
  size_t	  len=strlen(k->val);
  struct keyparse *kp;

  if (len==0) /* this is special, C-@ */
    len=1;
  kp=hfind(k->val,len,1);
  kp->key=k;
  while (--len>0) {
    kp=hfind(k->val,len,1);
    kp->key=NULL;
  }
}

void	out_addkey(const char *key,const char *name) {
  struct keydef	*k;

  k=chk_malloc(sizeof(*k));
  k->val=chk_strdup(key);
  k->name=chk_strdup(name);
  k->cap=NULL;
  addkey(k);
}

static char	inkeybuf[128];
static size_t	inkeylen;
static int	inkeymeta;

static void   dokey(const char *key,int len) {
  char	  buf[128];

  if (len<0)
    len=strlen(key);
  if (inkeymeta) {
    if ((size_t)len>sizeof(buf)-2)
      len=sizeof(buf)-2;
    buf[0]='M'; buf[1]='-';
    memcpy(buf+2,key,(size_t)len);
    inkeymeta=0;
    key=buf; len+=2;
  }
  window_process_key(key,len);
}

static void	inputchar(char ch) {
  struct keyparse *kp;
  size_t	  i;

  inkeybuf[inkeylen++]=ch;
  do {
    kp=hfind(inkeybuf,inkeylen,0);
    if (kp) {
      if (kp->key) {
	inkeylen=0;
	dokey(kp->key->name,-1);
	return;
      }
      if (inkeylen<sizeof(inkeybuf))
	return;
    }
    if (inkeybuf[0]=='\033') {
      if (inkeymeta) { /* M-C-[ */
	dokey("M-C-[",-1);
	inkeymeta=0;
      } else
	inkeymeta=1;
    } else
      dokey(inkeybuf,1);
    --inkeylen;
    for (i=0;i<inkeylen;++i)
      inkeybuf[i]=inkeybuf[i+1];
  } while (inkeylen>0);
}

void out_process_input(void) {
  char	  buffer[128];
  int	  n,i;

  n=read(0,buffer,sizeof(buffer));
  if (n<0) { /* doh, read error??? */
    if (errno==EINTR)
      return;
    n=0; /* treat other errors as EOF */
  }
  if (n==0) { /* eof or error */
    window_process_key("eof",-1);
    return;
  }
  for (i=0;i<n;++i)
    inputchar(buffer[i]);
}

static void keyinit(void) {
  size_t	i;
  static int	done;

  if (done)
    return;
  done=1;
  for (i=0;i<NTKEYS;++i) {
    if (!tkeys[i].val && tkeys[i].cap)
      tkeys[i].val=getcap(tkeys[i].cap);
    if (tkeys[i].val)
      addkey(tkeys+i);
  }
}

const char  *out_setup(int how) {
  static struct termios	  saveterm;
  struct termios	  term;
  const char		  *tname;

  if (how) {
    setupterm(0,1,0); /* use default settings */
    /* check if the terminal is good enough */
    /* - must have insert char/delete char caps */
    t_ich1=getcap("ich1");
    t_ich=getcap("ich");
    t_smir=getcap("smir");
    t_rmir=getcap("rmir");
    t_dch1=getcap("dch1");
    t_dch=getcap("dch");
    if ((!t_ich1 && !t_ich && !(t_smir && t_rmir)) || (!t_dch1 && !t_dch))
      return "terminal cannot insert/delete characters";
    /* - must have insert/delete line or set scrolling region caps */
    if (!(t_csr=getcap("csr"))) {
      t_il1=getcap("il1");
      t_il=getcap("il");
      t_dl1=getcap("dl1");
      t_dl=getcap("dl");
      if ((!t_il1 && !t_il) || (!t_dl1 && !t_dl))
	return "terminal cannot insert/delete lines";
    }
    /* - must have clear screen */
    if ((t_clear=getcap("clear")))
      tf_clear=1;
    else if (!(t_clear=getcap("ed")))
      return "terminal cannot clear screen";
    /* must have clear to end of line */
    if (!(t_clearline=getcap("el")))
      return "terminal cannot clear to end of line";
    /* - must have cursor position */
    if (!(t_cup=getcap("cup")))
      return "terminal cannot position cursor";
    t_smkx=getcap("smkx");
    t_rmkx=getcap("rmkx");
    t_smcup=getcap("smcup");
    t_rmcup=getcap("rmcup");
    t_smam=getcap("smam");
    t_rmam=getcap("rmam");
    t_sc=getcap("sc");
    t_rc=getcap("rc");
    if (getenv("COLORTERM") ||
        ((tname=getenv("TERM")) && !strcmp(tname,"xterm-color")))
      set_attrs=color_xterm_setattrs;
    else {
      t_smul=getcap("smul");
      t_rmul=getcap("rmul");
      t_bold=getcap("bold");
      t_sgr0=getcap("sgr0");
      t_rev=getcap("rev");
      t_setaf=getcap("setaf");
      t_setab=getcap("setab");
      if (t_setaf)
	set_attrs=color_setattrs;
      else
	set_attrs=mono_setattrs;
    }
    tcgetattr(0,&saveterm);
    term=saveterm;
    term.c_cc[VMIN]=1;
    term.c_cc[VTIME]=0;
    term.c_lflag&=~(ECHO|ICANON|ISIG);
    term.c_iflag&=~(ISTRIP|IXON|IXOFF);
    term.c_oflag&=~OPOST;
    tcsetattr(0,TCSANOW,&term);
    /* init terminal */
#ifdef MMCDEBUG
    setvbuf(stdout,NULL,_IONBF,0);
#else
    setvbuf(stdout,NULL,_IOFBF,0);
#endif
    if (t_smcup)
      tputs(t_smcup,1,winputchar);
    if (t_smkx)
      tputs(t_smkx,1,winputchar);
    if (t_rmam)
      tputs(t_rmam,1,winputchar);
    else
      cant_write_to_last_column=1;
    /* setup keys */
    keyinit();
    /* setup resize handler */
    signal(SIGWINCH,sigwinch);
  } else {
    if (t_csr)
      out_setscroll(0,height-1);
    out_movecursor(0,height-1);
    winprintf("\033[m\n");
    if (t_rmkx)
      tputs(t_rmkx,1,winputchar);
    if (t_rmcup)
      tputs(t_rmcup,1,winputchar);
    if (t_smam && tigetflag("am")>0)
      tputs(t_smam,1,winputchar);
    printf("%d bytes written to the terminal, %.2f%% escape sequences\r\n",esc_chars+norm_chars,(float)esc_chars*100/(esc_chars+norm_chars+1));
    fflush(stdout);
    tcsetattr(0,TCSANOW,&saveterm);
  }
  return NULL;
}

void	out_flush(void) {
  fflush(stdout);
}

void	out_playsound(const char *name) { /* not supported here */
  esc_chars++;
  putc('\a',stdout);
  fflush(stdout);
}
