#include "window.h"
#include "window_priv.h"
#include "output.h"
#include "cmalloc.h"
#include "timeout.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#ifndef WIN32
#include <unistd.h>
#endif

#undef lines
#ifdef WIN32
#define	snprintf	_snprintf
#define	HAVE_SNPRINTF	1
#endif

typedef struct {
  int	  len;
  int	  flag;
  char	  str[1];
} String;

/* we support up to 10 windows */
static struct Window {
  int		flags;	  /* window flags */
  int		lines;	  /* number of real lines in scrollback buffer */
  int		ptr;	  /* top of scrollback pointer */
  int		max;	  /* max number of real lines in scrollback buffer */
  int		vlines;	  /* number of virtual (wrapped) lines */
  String	**text;	  /* scrollback contents */
  int		sbtop;	  /* top visible scrollback line */
  int		sbpos;	  /* virtual bottom line when scrollback started */
  int		buflines; /* number of buffered, not yet output lines */
} windows[10];

enum {
  WF_VALID    = 1,	  /* window structure has valid contents */
  WF_SBW      = 2	  /* split scrollback is active in this window */
};

static String **g_status; /* status line contents */
static String *g_input;	  /* input line contents */
static int    g_ix;	  /* cursor position in input line */

static int    width;	  /* screen width */
static int    height;	  /* screen height */
static int    sbw_height; /* split scrollback height */
static int    status_mode=2;/* status line place/visibility */
static int    status_height=1;/* number of status lines */
static int    sblines=10000;/* default number of scrollback lines */
static int    cur_win=-1; /* visible window number */
static int    inputy;	  /* input line row */
static int    statusy;	  /* status line first row */

static int    status_bg=4;  /* status background */
static int    input_bg=-1;	  /* input background */
static int    text_bg=-1;	  /* text background */
static int    sbstatus_bg=3;  /* scrollback status background */

static int    slowscroll;   /* flush output after each line */

/* some local functions */
static void   redraw(int w,int clear,int all);
static void   update_sbstatus(struct Window *w,int how);

/* callback */
static WindowCB	wincb;
static void	*wincbdata;

static void wcall(int w,const char *key,int len) {
  if (!wincb)
    return;
  if (w<0)
    w=cur_win;
  if (len<0 && key)
    len=strlen(key);
  wincb(w,key,len,wincbdata);
}

/* strings support */
static String	  *s_new(int len,int flag,int color) {
  int      i;

  String  *s=chk_malloc(sizeof(String)+2*len);
  s->len=len;
  s->flag=flag;
  if (color>=0)
    for (i=0;i<2*len;i+=2) {
      s->str[i]=' ';
      s->str[i+1]=color;
    }
  return s;
}

static String	  *s_grow(String *s,int newlen,int color) {
  int	    i;

  String    *d=chk_realloc(s,sizeof(String)+2*newlen);
  if (!s)
    d->len=0;
  if (newlen>d->len && color>=0)
    for (i=2*d->len;i<2*newlen;i+=2) {
      d->str[i]=' ';
      d->str[i+1]=color;
    }
  d->len=newlen;
  return d;
}

void  window_resize(int neww,int newh) {
  int	i,j;

  height=newh;
  sbw_height=(height-status_height-1)*2/3;
  switch (status_mode) {
    case 1: /* ircII style status */
      inputy=height-1;
      statusy=height-1-status_height;
      break;
    case 2: /* input is before status */
      inputy=height-1-status_height;
      statusy=height-status_height;
      break;
    default: /* no status line at all */
      inputy=height-1;
      statusy=-1;
      break;
  }
  if (width!=neww) {
    width=neww;
    for (i=0;i<10;++i) {
      if (!(windows[i].flags&WF_VALID))
	continue;
      /* recalculate vlines */
      windows[i].vlines=0;
      for (j=0;j<windows[i].lines;++j)
	windows[i].vlines+=(windows[i].text[(j+windows[i].ptr)%windows[i].max]->len-1)/width+1;
      /* disable scrollback after width change */
      windows[i].flags&=~WF_SBW;
    }
    /* resize input line */
    g_input=s_grow(g_input,width,7);
    /* resize status lines */
    for (j=0;j<status_height;++j)
      g_status[j]=s_grow(g_status[j],width,7);
  }
  if (cur_win>=0)
    redraw(cur_win,0,1);
  else
    out_setscroll(0,height-2-status_height);
  wcall(-1,"RDR",-1);
}

static int setup_complete=0;

const char *window_init(void) {
  int	      i;
  const char  *emsg;

  if (setup_complete)
    return 0;
  /* setup terminal modes */
  if ((emsg=out_setup(1)))
    return emsg;
  setup_complete=1;
  /* create status lines */
  if (status_mode) {
    g_status=chk_malloc(status_height*sizeof(String*));
    for (i=0;i<status_height;++i)
      g_status[i]=NULL;
  }
  /* create input line */
  g_input=NULL;
  /* get sizes */
  out_update_winsize();
  /* setup stdio buffering */
  return NULL;
}

void	window_done(void) {
  int	i;

  if (!setup_complete)
    return;
  /* flush everything */
  window_flush();
  /* close all windows */
  cur_win=-1;
  for (i=0;i<10;++i)
    if (windows[i].flags&WF_VALID)
      window_close(i);
  if (status_mode) {
    for (i=0;i<status_height;++i)
      free(g_status[i]);
    free(g_status);
  }
  free(g_input);
  out_setup(0);
  setup_complete=0;
}

void	window_getsize(int *w,int *h,int *iw) {
  *w=width;
  *h=height;
  if (inputy==height-1)
    *iw=width-1;
  else
    *iw=width;
}

#define	ICHECK()  do { if (!setup_complete) return; } while (0)
#define	WCHECK()  do { if (w<0 || w>=10 || !(windows[w].flags&WF_VALID)) return; } while (0)
#define	XCHECK()  do { if (x<0 || x>=width) return; } while (0)
#define	LCHECK()  do { if (x+len>width) len=width-x; } while (0)

int	window_open(void) {
  int	  i;

  if (!setup_complete)
    return -1;
  for (i=0;i<10;++i)
    if (!(windows[i].flags&WF_VALID))
      goto found;
  return -1;
found:
  memset(&windows[i],0,sizeof(windows[i]));
  windows[i].max=sblines;
  windows[i].text=chk_malloc(sblines*sizeof(String*));
  memset(windows[i].text,0,sblines*sizeof(String*));
  windows[i].flags=WF_VALID;
  if (cur_win<0) {
    cur_win=i;
    redraw(i,1,1);
  }
  return i;
}

void	window_close(int w) {
  int	i,j;

  ICHECK(); WCHECK();
  windows[w].flags=0;
  for (i=0;i<windows[w].max;++i)
    free(windows[w].text[i]);
  free(windows[w].text);
  if (cur_win==i) {
    for (j=0;j<10;++j)
      if (windows[(i+j)%10].flags&WF_VALID) {
	cur_win=(i+j)%10;
	goto ok;
      }
    cur_win=-1;
    return;
ok: redraw(cur_win,0,0);
  }
}

void	window_set_status_mode(int mode,int sh) {
  int	update=0,i,oldh;

  if (mode<0 || mode>=3 || sh<0)
    return;
  if (!mode)
    sh=0;
  if (!sh)
    mode=0;
  oldh=status_mode ? status_height : -1;
  if (mode!=status_mode) {
    status_mode=mode;
    update=1;
  }
  if (sh!=status_height) {
    status_height=sh;
    update=1;
  }
  if (!setup_complete)
    return;
  if (update) {
    /* reallocate status lines */
    if (status_mode) {
      if (oldh>0) { /* reallocate */
	if (oldh!=status_height) {
	  for (i=status_height;i<oldh;++i)
	    free(g_status[i]);
	  g_status=chk_realloc(g_status,status_height*sizeof(String*));
	  for (i=oldh;i<status_height;++i)
	    g_status[i]=s_new(width,0,7);
	}
      } else { /* create new */
	g_status=chk_malloc(status_height*sizeof(String*));
	for (i=0;i<status_height;++i)
	  g_status[i]=s_new(width,0,7);
      }
    } else { /* delete all */
      for (i=0;i<oldh;++i)
	free(g_status[i]);
      free(g_status);
    }
    /* redraw screen */
    window_resize(width,height);
  }
}

static char   *addline(struct Window *w,int len,int flag) {
  int	    n;
  String    *text;

  text=s_new(len,flag,-1);
  n=(w->lines+w->ptr)%w->max;
  if (w->lines>=w->max) {
    w->vlines-=(w->text[n]->len-1)/width+1;
    free(w->text[n]);
    ++w->ptr;
  } else
    ++w->lines;
  w->text[n]=text;
  w->vlines+=(len-1)/width+1;
  return text->str;
}

int    window_fetchline(int w,int idx,const char **line,int *len,int *flag) {
  if (w<0 || w>=10 || !(windows[w].flags&WF_VALID) || idx<0 || idx>=windows[w].lines)
    return 0;
  idx=(idx+windows[w].ptr)%windows[w].max;
  *line=windows[w].text[idx]->str;
  *len=windows[w].text[idx]->len;
  *flag=windows[w].text[idx]->flag;
  return 1;
}

int    window_getline(struct Window *w,int idx,const char **line,int *len) {
  int	    last, count, ptr;

  if (idx<0 || idx>=w->vlines)
    return -1;
  last=(w->lines+w->ptr-1)%w->max;
  count=w->lines;
  ptr=-1;
  while (count--) {
    int	    cur=1+(w->text[last]->len-1)/width;
    if (idx<=ptr+cur && idx>ptr) {
      int	off=width*(ptr+cur-idx);
      *len=w->text[last]->len-off;
      if (*len>width)
	*len=width;
      *line=w->text[last]->str+off*2;
      return 0;
    }
    ptr+=cur;
    if (--last<0)
      last+=w->max;
  }
  return -1;
}

#define	wout_notify(w)	wcall(w,NULL,WMSG_ACT)

static void slowcheck(void) {
  static int	slowcount;
  if (slowscroll) {
    if (slowscroll<0) {
      if (--slowcount<=slowscroll) {
	window_flush();
	slowcount=0;
      }
    } else {
      window_flush();
      delay(slowscroll);
    }
  }
}

void	window_output_ctext(int w,int flag,const char *ctext,int len) {
  char	  *b;

  ICHECK(); WCHECK();
  b=addline(windows+w,len,flag);
  memcpy(b,ctext,2*len);
  if (w==cur_win)
    windows[w].buflines+=(len-1)/width+1;
  if (windows[w].flags&WF_SBW) {
    windows[w].sbtop+=(len-1)/width+1;
    windows[w].sbpos+=(len-1)/width+1;
  }
  slowcheck();
  wout_notify(w);
}

void	window_output_text(int w,int flag,const char *text,int len,int color) {
  char	  *b;
  int	  i;

  ICHECK(); WCHECK();
  b=addline(windows+w,len,flag);
  for (i=0;i<2*len;i+=2) {
    b[i]=*text++;
    b[i+1]=color;
  }
  if (w==cur_win)
    windows[w].buflines+=(len-1)/width+1;
  if (windows[w].flags&WF_SBW) {
    windows[w].sbtop+=(len-1)/width+1;
    windows[w].sbpos+=(len-1)/width+1;
  }
  slowcheck();
  wout_notify(w);
}

void	window_flush(void) {
  ICHECK();
  if (cur_win>=0 && windows[cur_win].buflines>0) {
    out_drawlines(windows+cur_win,windows[cur_win].buflines,text_bg);
    windows[cur_win].buflines=0;
    if (windows[cur_win].flags&WF_SBW)
      update_sbstatus(windows+cur_win,0);
  }
  out_movecursor(g_ix,inputy);
  out_flush();
}

void	window_output_cstatus(int x,int y,const char *ctext,int len) {
  ICHECK(); XCHECK(); LCHECK();
  if (!status_mode || y<0 || y>=status_height)
    return;
  memcpy(g_status[y]->str+2*x,ctext,2*len);
  out_movecursor(x,statusy+y);
  out_cwrite(ctext,len,status_bg);
  out_setcolor(-1,text_bg);
}

void	window_output_status(int x,int y,const char *text,int len,int color) {
  int	      i;
  const char  *p;

  ICHECK(); XCHECK(); LCHECK();
  if (!status_mode || y<0 || y>=status_height)
    return;
  for (i=2*x,p=text;i<2*(x+len);i+=2) {
    g_status[y]->str[i]=*p++;
    g_status[y]->str[i+1]=color;
  }
  out_movecursor(x,statusy+y);
  out_setcolor(color,status_bg);
  out_rawrite(text,len);
  out_setcolor(-1,text_bg);
}

void	window_output_cinput(int x,const char *ctext,int len) {
  ICHECK(); XCHECK(); LCHECK();
  memcpy(g_input->str+2*x,ctext,2*len);
  out_movecursor(x,inputy);
  out_cwrite(ctext,len,input_bg);
  g_ix=x+len;
}

void	window_output_input(int x,const char *text,int len,int color) {
  int	      i;
  const char  *p;

  ICHECK(); XCHECK(); LCHECK();
  for (i=2*x,p=text;i<2*(x+len);i+=2) {
    g_input->str[i]=*p++;
    g_input->str[i+1]=color;
  }
  out_movecursor(x,inputy);
  out_setcolor(color,input_bg);
  out_rawrite(text,len);
  g_ix=x+len;
}

void	window_insert_input(int x,int len) {
  int	  i;

  ICHECK(); XCHECK(); LCHECK();
  memmove(g_input->str+2*(x+len),g_input->str+2*x,2*(width-x-len));
  for (i=2*x;i<2*(x+len);i+=2)
    g_input->str[i]=' ';
  out_movecursor(x,inputy);
  out_setcolor(11,input_bg);
  out_inschars(len);
  g_ix=x;
}

void	window_delete_input(int x,int len) {
  int	  i;

  ICHECK(); XCHECK(); LCHECK();
  memmove(g_input->str+2*x,g_input->str+2*(x+len),2*(width-x-len));
  for (i=2*(width-len);i<2*width;i+=2)
    g_input->str[i]=' ';
  out_movecursor(x,inputy);
  out_delchars(len);
  g_ix=x;
}

void	window_deol_input(void) {
  int	  i;

  ICHECK();
  for (i=2*g_ix;i<2*width;i+=2) {
    g_input->str[i]=' ';
    g_input->str[i+1]=7;
  }
  out_clearline();
}

void	window_move_icursor(int x) {
  ICHECK(); XCHECK();
  g_ix=x;
  out_movecursor(x,inputy);
}

void	window_move_icursor_left(void) {
  ICHECK();
  if (g_ix>0) {
    --g_ix;
    out_movecursor(g_ix,inputy);
  }
}

void	window_move_icursor_right(void) {
  window_move_icursor(g_ix+1);
}

int	window_input_pos(void) {
  return g_ix;
}

void	window_set_cb(WindowCB cb,void *data) {
  wincb=cb;
  wincbdata=data;
}

void	window_set_confvar(int id,int var) {
  switch (id) {
    case WCF_SBSIZE:
      sblines=var;
      break;
    case WCF_INPUTBG:
      input_bg=var;
      break;
    case WCF_STATUSBG:
      status_bg=var;
      break;
    case WCF_TEXTBG:
      text_bg=var;
      break;
    case WCF_SBSTATUSBG:
      sbstatus_bg=var;
      break;
    case WCF_SLOWSCROLL:
      slowscroll=var;
      break;
  }
}

void	window_playsound(const char *name) {
  out_playsound(name);
}

static void   sbdraw(struct Window *w) {
  int	      i;
  const char  *text;
  int	      len;

  for (i=0;i<sbw_height;++i) {
    out_movecursor(0,i);
    if (window_getline(w,w->sbtop-i,&text,&len)>=0) {
      out_cwrite(text,len,text_bg);
      if (len!=width)
	out_clearline();
    } else
      out_clearline();
  }
  out_movecursor(0,sbw_height);
  if (w->flags&WF_SBW)
    update_sbstatus(w,1);
  else {
    if (window_getline(w,w->sbtop-sbw_height,&text,&len)>=0) {
      out_cwrite(text,len,text_bg);
      if (len!=width)
	out_clearline();
    } else
      out_clearline();
  }
}

static void   redraw(int w,int clear,int all) {
  int	      i,bot;
  const char  *text;
  int	      len;

  if (clear)
    out_clearscr();
  out_setscroll((windows[w].flags & WF_SBW) ? sbw_height+1 : 0,height-2-status_height);
  /* draw scrollback + output */
  bot=height-1-status_height;
  if (windows[w].flags&WF_SBW) {
    sbdraw(windows+w);
    i=sbw_height+1;
  } else
    i=0;
  if (clear) {
    for (;i<bot;++i) {
      if (window_getline(windows+w,bot-i-1,&text,&len)>=0) {
	out_movecursor(0,i);
	out_cwrite(text,len,text_bg);
      }
    }
  } else {
    for (;i<bot;++i) {
      out_movecursor(0,i);
      if (window_getline(windows+w,bot-i-1,&text,&len)>=0) {
	out_cwrite(text,len,text_bg);
	if (len!=width)
	  out_clearline();
      } else
	out_clearline();
    }
  }
  if (all) {
    /* draw status line */
    if (status_mode) {
      for (i=0;i<status_height;++i) {
	out_movecursor(0,statusy+i);
	out_cwrite(g_status[i]->str,width,status_bg);
      }
      out_setcolor(-1,text_bg);
    }
    /* draw input line */
    out_movecursor(0,inputy);
    out_cwrite(g_input->str,width,input_bg);
  }
  windows[w].buflines=0;
}

static void   update_sbstatus(struct Window *w,int how) {
  int	    i;
  char	    buf[1024]; /* hope this is big enough for two integers */
		      /* too bad that we have to support ancient braindamaged */
		      /* systems without snprintf */

  out_setcolor(4,sbstatus_bg);
  if (how) { /* repaint all */
    out_movecursor(0,sbw_height);
#ifdef HAVE_SNPRINTF
    i=snprintf(buf,sizeof(buf),"    %d+%d",w->sbtop-w->sbpos,w->sbpos);
    if (i<0 || i>sizeof(buf))
      i=sizeof(buf);
#else
    i=sprintf(buf,"    %d+%d",w->sbtop-w->sbpos,w->sbpos);
#endif
    while (i<width && i<sizeof(buf))
      buf[i++]=' ';
    out_rawrite(buf,i);
  } else {
    out_movecursor(4,sbw_height);
#ifdef HAVE_SNPRINTF
    i=snprintf(buf,sizeof(buf),"%d+%d",w->sbtop-w->sbpos,w->sbpos);
    if (i<0 || i>sizeof(buf))
      i=sizeof(buf);
#else
    i=sprintf(buf,"%d+%d",w->sbtop-w->sbpos,w->sbpos);
#endif
    out_rawrite(buf,i);
  }
  out_setcolor(-1,text_bg);
}

void	window_switch(int w) {
  if (w<0 || w>=10 || cur_win==w || !(windows[w].flags&WF_VALID))
    return;
  cur_win=w;
  redraw(w,0,0);
}

void	window_sbup(int amount) {
  if (cur_win<0)
    return;
  if (amount<=0)
    amount=sbw_height-2;
  if (amount>sbw_height-1)
    amount=sbw_height-1;
  if (windows[cur_win].flags&WF_SBW) {
    if (windows[cur_win].sbtop>=windows[cur_win].vlines)
      return;
    windows[cur_win].sbtop+=amount;
    sbdraw(windows+cur_win);
  } else {
    if (windows[cur_win].vlines<=height-1-status_height)
      return;
    out_setscroll(sbw_height+1,height-2-status_height);
    windows[cur_win].sbtop=height-1-status_height+sbw_height-1;
    windows[cur_win].sbpos=0;
    windows[cur_win].flags|=WF_SBW;
    sbdraw(windows+cur_win);
  }
}

void	window_sbdown(int amount) {
  if (cur_win<0 || !(windows[cur_win].flags&WF_SBW))
    return;
  if (amount==0)
    amount=sbw_height-2;
  if (amount>sbw_height-1)
    amount=sbw_height-1;
  windows[cur_win].sbtop-=amount;
  if (amount<0 || windows[cur_win].sbtop<=height-1-status_height) {
    out_setscroll(0,height-2-status_height);
    windows[cur_win].sbtop=height-1-status_height-1;
    windows[cur_win].flags&=~WF_SBW;
  }
  sbdraw(windows+cur_win);
}

void	window_process_key(const char *key,int len) {
  wcall(-1,key,len);
}

void	window_redraw(void) {
  if (cur_win>=0 && (windows[cur_win].flags&WF_VALID))
    redraw(cur_win,1,1);
}
