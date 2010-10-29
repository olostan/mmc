#include <windows.h>
#include <windowsx.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

#include "output.h"
#include "window.h"
#include "window_priv.h"
#include "socket.h"

#include "resource.h"

#define	XLEFTPAD  2

#define	M_KEY	  WM_APP
#define	M_RESIZE  (WM_APP+1)
#define	M_CLOSE	  (WM_APP+2)
#define	M_BYEBYE  (WM_APP+3)
#define	M_PASTE	  (WM_APP+4)
#define M_PIPEDATA (WM_APP+5)

#define	CMD_CEXIT     1
#define	CMD_SELFONT   2
#define	CMD_SELCOLOR  3
#define	CMD_XTERM     4
#define	CMD_SAVEPOS   5
#define	CMD_DEBUG     6
#define	CMD_EATMENU   7

#define	KF_KEY	  1
#define	KF_ALT	  2
#define	KF_CTL	  4
#define	KF_SHIFT  8
#define	KF_PAD	  16

#define	REGPATH	  "Software\\Haali\\MMC"

#define	MAXCOLORS	256

#define	CF_CHG		1
#define	CF_CURSOR	2
#define	CF_SELECTION	4

#define	DEFAULT_FG	7
#define	DEFAULT_BG	0

#define	RESERVED_COLORS_START	200
#define	CURSOR_FG		200
#define	CURSOR_BG		201
#define	SELECTED_FG		202
#define	SELECTED_BG		203
#define	SELECTED_CURSOR_FG	204
#define	SELECTED_CURSOR_BG	205
#define	RESERVED_COLORS_END	206

struct pipedata {
  DWORD size;
  char  buf[1];
};

typedef struct {
    unsigned char	ch;
    unsigned char	fg;
    unsigned char	bg;
    unsigned char	flags;
} CELL;

__inline int	CEQ(const CELL *p,const CELL *q) {
    return p->ch==q->ch && p->fg==q->fg && p->bg==q->bg;
}

typedef struct {
    int		    width,height;
    int		    ch_width,ch_height;
    int		    pix_width,pix_height;
    int		    cursorx,cursory;
    int		    vcursorx,vcursory;
    int		    padx,pady;
    int		    srtop,srbot;
    int		    selstart,selend,vselstart,vselend;
    int		    selecting;
    unsigned char   fg,bg;
    CELL	    *data;
    int		    *vchars;
} SCREEN;

static void PasteClip(void);

static __inline CELL *LINE(SCREEN *scr,int y) { return scr->data+y*scr->width; }
static __inline int  XY2C(SCREEN *scr,int x,int y) { return scr->width*y+x; }
static __inline void GETSEL(SCREEN *scr,int *ss,int *se) {
  *ss=scr->vselstart; *se=scr->vselend;
}
static int	     INSEL(SCREEN *scr,int c1,int c2) {
  int	ss,se;
  GETSEL(scr,&ss,&se);
  return (c1>=ss && c1<se) ||
	 (c2>=ss && c2<se) ||
	 (ss>=c1 && ss<c2) ||
	 (se>=c1 && se<c2);
}
static __inline	int  INSELXYL(SCREEN *scr,int x,int y,int l) {
  int	pt=XY2C(scr,x,y);
  return INSEL(scr,pt,pt+l);
}
static __inline int  INSELY(SCREEN *scr,int y,int n) {
  int	pt=y*scr->width;
  return INSEL(scr,pt,pt+n*scr->width);
}
static __inline void C2XY(SCREEN *scr,int c,int *x,int *y) {
  *x=c%scr->width;
  *y=c/scr->width;
}

static COLORREF	colormap[MAXCOLORS]={ /* initialize first 16 colors */
  RGB(0,0,0),
  RGB(192,0,0),
  RGB(0,192,0),
  RGB(192,192,0),
  RGB(0,0,192),
  RGB(192,0,192),
  RGB(0,192,192),
  RGB(192,192,192),
  RGB(128,128,128),
  RGB(255,0,0),
  RGB(0,255,0),
  RGB(255,255,0),
  RGB(0,0,255),
  RGB(255,0,255),
  RGB(0,255,255),
  RGB(255,255,255),
};

static SCREEN	*curscr;
static HFONT	hScrFont;
static HWND	hMainWnd;
static DWORD	hMainThreadID;
static int	initdone;
static HANDLE	hGUIThread;
static HANDLE	hScrMutex;
static HINSTANCE  g_hInst;
static int	bibi;
static HANDLE	hSizeEvent;
static int	autoclose;
static HKEY	hAppKey;
static HMENU	hSysMenu;
static int	nextfulldraw;
static int	xtermbuttons;
static int	ignoredestroy;
static int	eatmenu;

#define	LOCKSCR()   WaitForSingleObject(hScrMutex,INFINITE)
#define	UNLOCKSCR() ReleaseMutex(hScrMutex)

static void	dmsg(const char *fmt,...) {
  va_list   ap;
  char	    buf[512];

  va_start(ap,fmt);
  _vsnprintf(buf,sizeof(buf),fmt,ap);
  va_end(ap);
  MessageBox(hMainWnd,buf,"Debug",MB_OK);
}

static void	nomem(void) {
    MessageBox(hMainWnd,"Out of memory","Error",MB_ICONERROR|MB_OK);
    exit(1);
}

static char	*regstr(const char *name) {
  char	      *buffer;
  DWORD	      type,size;

  if (RegQueryValueEx(hAppKey,name,0,&type,NULL,&size)!=ERROR_SUCCESS)
    return 0;
  if (type!=REG_SZ)
    return 0;
  buffer=malloc(size);
  if (!buffer)
    return 0;
  if (RegQueryValueEx(hAppKey,name,0,&type,buffer,&size)!=ERROR_SUCCESS) {
    free(buffer);
    return 0;
  }
  return buffer;
}

/* unfortunately M$ implementation of vsnprintf is brandamaged since it doesnt
   return the required buffer size on overflow, so i can't even allocate a
   properly sized buffer if i need to print more than 512 bytes */
static int	regprintf(const char *name,const char *fmt,...) {
  char	      buffer[512];
  va_list     ap;
  int	      n;

  va_start(ap,fmt);
  n=_vsnprintf(buffer,sizeof(buffer),fmt,ap);
  va_end(ap);
  if (n<0) { /* it overflowed */
    n=sizeof(buffer)-1;
    buffer[sizeof(buffer)-1]='\0';
  }
  RegSetValueEx(hAppKey,name,0,REG_SZ,buffer,n+1);
  return n;
}

static DWORD pipe_helper(HANDLE *hp) {
  HANDLE    rpipe=hp[0];
  int	    idx=(int)hp[1];
  DWORD	    nr;
  char	    buf[8192];
  struct pipedata *pd;

  free(hp);
  for (;;) {
    if (!ReadFile(rpipe,buf,sizeof(buf),&nr,NULL) || nr==0) {
      CloseHandle(rpipe);
      PostThreadMessage(hMainThreadID,M_PIPEDATA,idx,0);
      break;
    }
    pd=malloc(sizeof(*pd)+nr-1);
    if (pd) {
      // oem-ansi translation
      OemToCharBuff(buf,pd->buf,nr);
      pd->size=nr;
      PostThreadMessage(hMainThreadID,M_PIPEDATA,idx,(LPARAM)pd);
    }
  }
  return 0;
}

BOOL  StartExtCmd(char *cmd,int idx) {
  /* create a new process, for windows this is quite convoluted */
  HANDLE	      hOutRTmp=NULL,hOutR,hOutW=NULL,hErrW=NULL,*hp,hThread;
  STARTUPINFO	      si;
  SECURITY_ATTRIBUTES sa;
  PROCESS_INFORMATION pi;
  DWORD		      tid;

  sa.nLength=sizeof(sa);
  sa.lpSecurityDescriptor=NULL;
  sa.bInheritHandle=TRUE;

  if (!CreatePipe(&hOutRTmp,&hOutW,&sa,0) ||
      !DuplicateHandle(GetCurrentProcess(),hOutW,GetCurrentProcess(),&hErrW,
	0,TRUE,DUPLICATE_SAME_ACCESS) ||
      !DuplicateHandle(GetCurrentProcess(),hOutRTmp,GetCurrentProcess(),&hOutR,
	0,FALSE,DUPLICATE_SAME_ACCESS))
    goto out;
  CloseHandle(hOutRTmp);

  memset(&si,0,sizeof(si));
  si.cb=sizeof(si);
  si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
  si.hStdOutput=hOutW;
  si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
  si.hStdError=hErrW;
  si.wShowWindow=SW_HIDE;

  if (!CreateProcess(NULL,cmd,NULL,NULL,TRUE,CREATE_NO_WINDOW|DETACHED_PROCESS,
	NULL,NULL,&si,&pi))
    goto out;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  CloseHandle(hOutW);
  CloseHandle(hErrW);

  /* for some reason you can't Wait() on the pipe handle to see if some data is
     available, so I have to create a thread */
  hp=malloc(2*sizeof(HANDLE));
  if (!hp)
    goto out;
  hp[0]=hOutR;
  hp[1]=(HANDLE)idx;
  hThread=CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)&pipe_helper,hp,0,&tid);
  if (hThread==NULL) {
    free(hp);
    goto out;
  }
  CloseHandle(hThread);
  return TRUE;
out:
  CloseHandle(hOutW);
  CloseHandle(hErrW);
  CloseHandle(hOutR);
  return FALSE;
}

static void	WriteStr(HDC hDC,
			 SCREEN *scr,
			 int x0,
			 int y0,
			 int xs,
			 CELL *ch,
			 int n)
{
    char	  locbuf[512];
    char	  *buffer=locbuf;
    int		  i;
    RECT	  r;

    if (n>sizeof(locbuf)) {
	buffer=malloc(n);
	if (!buffer)
	    return;
    }
    for (i=0;i<n;++i) { /* copy chars and mark cells as up to date */
	buffer[i]=ch[xs+i].ch;
	ch[xs+i].flags&=~CF_CHG;
    }
    r.left=x0+scr->ch_width*xs;
    r.top=y0;
    r.right=r.left+n*scr->ch_width;
    r.bottom=r.top+scr->ch_height;
    switch (ch[xs].flags&(CF_CURSOR|CF_SELECTION)) {
      case CF_CURSOR:
	SetTextColor(hDC,colormap[CURSOR_FG]);
	SetBkColor(hDC,colormap[CURSOR_BG]);
	break;
      case CF_SELECTION:
	SetTextColor(hDC,colormap[SELECTED_FG]);
	SetBkColor(hDC,colormap[SELECTED_BG]);
	break;
      case CF_CURSOR|CF_SELECTION:
	SetTextColor(hDC,colormap[SELECTED_CURSOR_FG]);
	SetBkColor(hDC,colormap[SELECTED_CURSOR_BG]);
	break;
      default:
	SetTextColor(hDC,colormap[ch[xs].fg]);
	SetBkColor(hDC,colormap[ch[xs].bg]);
	break;
    }
    ExtTextOut(hDC,r.left,r.top,ETO_CLIPPED|ETO_OPAQUE,
	&r,buffer,n,NULL);
    if (buffer!=locbuf)
	free(buffer);
}

static void UpdateScreen(SCREEN *scr,HDC hDC,RECT *cli) {
    int		x,y;
    int		y0=cli->top+scr->pady;

    if (scr->vcursorx!=scr->cursorx || scr->vcursory!=scr->cursory) {
	/* delete old cursor */
	LINE(scr,scr->vcursory)[scr->vcursorx].flags&=~CF_CURSOR;
	/* mark cell as changed */
	LINE(scr,scr->vcursory)[scr->vcursorx].flags|=CF_CHG;
	/* write new cursor */
	LINE(scr,scr->cursory)[scr->cursorx].flags|=CF_CHG|CF_CURSOR;
	/* remember new visible cursor coords */
	scr->vcursorx=scr->cursorx;
	scr->vcursory=scr->cursory;
    } else if (!(LINE(scr,scr->cursory)[scr->cursorx].flags&CF_CURSOR)) {
	/* write new cursor */
	LINE(scr,scr->cursory)[scr->cursorx].flags|=CF_CHG|CF_CURSOR;
    }
    for (y=0;y<scr->height;++y,y0+=scr->ch_height) { /* for each line */
	CELL	*line=LINE(scr,y);
	for (x=0;x<scr->width;) /* find a run of modified cells */
	    if (line[x].flags&CF_CHG) {
		int	      n=1;
		unsigned char flags=line[x].flags;
		unsigned char fg=line[x].fg;
		unsigned char bg=line[x].bg;
		for (;n+x<scr->width;++n)
		  if (line[n+x].flags!=flags ||
		    line[n+x].bg!=bg ||
		    line[n+x].fg!=fg)
		    break;
		WriteStr(hDC,scr,cli->left+XLEFTPAD,y0,x,line,n);
		x+=n;
	    } else
		++x;
    }
}

static SCREEN	*ResizeScreen(SCREEN *scr,int pixwidth,int pixheight,
			       int chwidth,int chheight,int markchg)
{
    CELL	  *nd;
    int		  *nl;
    int		  x,y;
    unsigned char flags;

    /* remove selection */
    scr->selstart=scr->selend=scr->vselstart=scr->vselend=0;
    scr->selecting=0;
    /* calc new dimensions */
    scr->ch_width=chwidth;
    scr->ch_height=chheight;
    scr->pix_width=pixwidth;
    scr->pix_height=pixheight;
    scr->width=scr->pix_width/scr->ch_width;
    scr->height=(scr->pix_height+scr->ch_height-1)/scr->ch_height;
    if (scr->width<10)
	scr->width=10;
    if (scr->height<4)
	scr->height=4;
    scr->padx=(scr->pix_width-scr->width*scr->ch_width-XLEFTPAD);
    if (scr->padx<0)
	scr->padx=0;
    scr->pady=(scr->pix_height-scr->height*scr->ch_height);
    /*if (scr->pady<0) vertical padding can be <0 now :)
	scr->pady=0; */
    /* reallocate data */
    nd=realloc(scr->data,scr->width*scr->height*sizeof(CELL));
    if (!nd) /* out of memory */
	nomem();
    nl=realloc(scr->vchars,scr->height*sizeof(int));
    if (!nl)
	nomem();
    scr->vchars=nl;
    scr->data=nd;
    /* completely erase old contents */
    flags=markchg?CF_CHG:0;
    for (y=0;y<scr->height;++y) {
	CELL	*line=LINE(scr,y);
	for (x=0;x<scr->width;++x) {
	    line[x].ch=' ';
	    line[x].fg=scr->fg;
	    line[x].bg=scr->bg;
	    line[x].flags=flags;
	}
	scr->vchars[y]=0;
    }
    /* reposition cursor */
    scr->vcursorx=scr->vcursory=scr->cursorx=scr->cursory=0;
    /* reset scrolling region */
    scr->srtop=scr->srbot=0;
    return scr;
}

static SCREEN	*CreateScreen(int pixwidth,int pixheight,
			       int chwidth,int chheight)
{
    SCREEN	*scr;

    if (pixwidth<=0 || pixheight<=0 || chwidth<=0 || chheight<=0)
	return NULL;
    scr=malloc(sizeof(SCREEN));
    if (!scr)
	nomem();
    scr->cursorx=scr->cursory=0;
    scr->vcursorx=scr->vcursory=0;
    scr->srtop=scr->srbot=0;
    scr->fg=DEFAULT_FG;
    scr->bg=DEFAULT_BG;
    scr->data=NULL;
    scr->vchars=NULL;
    return ResizeScreen(scr,pixwidth,pixheight,chwidth,chheight,1);
}

#define	CLAMP(v,l,h)  do { if ((v)<(l)) (v)=(l); if ((v)>=(h)) (v)=(h)-1; } while (0)

#ifndef NDEBUG
static LARGE_INTEGER  cliptimes;
static int	      clipcount;
#endif

static void	MarkRgnChg(HRGN hRgn,RECT *cli) {
  RECT	  ir,id;
  HRGN	  hLr;
  CELL	  *line;
  int	  x0,y0,x1,y1,x,y;
#ifndef NDEBUG
    LARGE_INTEGER start,end;
#endif

#ifndef NDEBUG
  /* let's measure some clipping times */
  QueryPerformanceCounter(&start);
#endif
  hLr=CreateRectRgn(0,0,0,0);
  GetRgnBox(hRgn,&ir);
  /* round coords */
  y0=(ir.top-cli->top-curscr->pady)/curscr->ch_height;
  y1=(ir.bottom-cli->top-curscr->pady+curscr->ch_height-2)/curscr->ch_height;
  CLAMP(y0,0,curscr->height);
  CLAMP(y1,0,curscr->height);
  /* mark cells as changed */
  ir.top=cli->top+curscr->pady+y0*curscr->ch_height;
  ir.bottom=ir.top+curscr->ch_height;
  ir.left=cli->left+XLEFTPAD;
  ir.right=ir.left+curscr->width*curscr->ch_width;
  line=LINE(curscr,y0);
  for (y=y0;y<=y1;++y) {
    SetRectRgn(hLr,ir.left,ir.top,ir.right,ir.bottom);
    if (CombineRgn(hLr,hLr,hRgn,RGN_AND)!=NULLREGION) {
      GetRgnBox(hLr,&id);
      x0=(id.left-cli->left-XLEFTPAD)/curscr->ch_width;
      x1=(id.right-cli->left-XLEFTPAD+curscr->ch_width-1)/curscr->ch_width;
      for (x=x0;x<x1;++x)
	line[x].flags|=CF_CHG;
    }
    ir.top+=curscr->ch_height;
    ir.bottom+=curscr->ch_height;
    line+=curscr->width;
  }
  DeleteObject(hLr);
#ifndef NDEBUG
  QueryPerformanceCounter(&end);
  cliptimes.QuadPart+=end.QuadPart;
  cliptimes.QuadPart-=start.QuadPart;
  clipcount++;
#endif
}

#ifndef NDEBUG
static LARGE_INTEGER  painttimes;
static int	      paintcount;
#endif

static void	PaintWindow(HDC hDC,HRGN hRgn,RECT *cli) {
  HGDIOBJ	old;
#ifndef NDEBUG
  LARGE_INTEGER start,end;
#endif

  if (!curscr)
    return;
  if (hRgn)
    MarkRgnChg(hRgn,cli);
#ifndef NDEBUG
  QueryPerformanceCounter(&start);
#endif
  /* and draw the thing */
  old=SelectObject(hDC,hScrFont);
  UpdateScreen(curscr,hDC,cli);
  SelectObject(hDC,old);
#ifndef NDEBUG
  QueryPerformanceCounter(&end);
  painttimes.QuadPart+=end.QuadPart;
  painttimes.QuadPart-=start.QuadPart;
  paintcount++;
#endif
}

#ifndef NDEBUG
static double	disp_paint_times(void) {
  LARGE_INTEGER	freq;
  double	cave=0,ctot,pave=0,ptot;

  QueryPerformanceFrequency(&freq);
  ctot=(double)cliptimes.QuadPart*1000.0/(double)freq.QuadPart;
  ptot=(double)painttimes.QuadPart*1000.0/(double)freq.QuadPart;
  if (clipcount)
    cave=ctot/clipcount;
  if (paintcount)
    pave=ptot/paintcount;
  dmsg(	"clip count=%d, total=%.3lg ms, ave=%.3lg ms\r\n"
	"paint count=%d, total=%.3lg ms, ave=%.3lg ms\r\n"
	"total paiting and clipping time=%.3lg ms",
	clipcount,ctot,cave,
	paintcount,ptot,pave,
	ptot+ctot);
  return 0;
}
#endif

static void ClearRect(HDC hDC,RECT *r,RECT *ir) {
  RECT	d;

  if (IntersectRect(&d,r,ir)) {
    HBRUSH	hbr;

    hbr=CreateSolidBrush(colormap[0]);
    FillRect(hDC,&d,hbr);
    DeleteObject(hbr);
  }
}

static void PadClear(HDC hDC,RECT *ir,RECT *cli) {
  RECT	  r;
  int	  vh,vw;

  if (!curscr)
    return;
  vw=curscr->width*curscr->ch_width;
  vh=curscr->height*curscr->ch_height;
  r.left=cli->left;
  r.right=r.left+XLEFTPAD;
  r.top=cli->top;
  r.bottom=cli->bottom;
  ClearRect(hDC,&r,ir);
  if (curscr->padx>0) {
    r.left=XLEFTPAD+vw;
    r.right=cli->right;
    r.top=cli->top;
    r.bottom=cli->bottom;
    ClearRect(hDC,&r,ir);
  }
  if (curscr->pady>0) {
    r.left=cli->left+XLEFTPAD;
    r.right=cli->right;
    r.top=cli->top;
    r.bottom=r.top+curscr->pady;
    ClearRect(hDC,&r,ir);
  }
}

void	win32_nomem(void) {
    nomem();
}

static void	PIX2CELL(SCREEN *scr,int px,int py,int *x,int *y) {
  px-=XLEFTPAD;
  if (px<0)
    px=0;
  px/=scr->ch_width;
  if (px>scr->width)
    px=scr->width;
  py-=scr->pady;
  if (py<0)
    py=0;
  py/=scr->ch_height;
  if (py>=scr->height)
    py=scr->height-1;
  *x=px; *y=py;
}

static void	MarkSelRgn(SCREEN *scr,int ss,int se) {
  int	    xs,ys,xe,ye,x1,x2;
  int	    x,y;

  C2XY(scr,ss,&xs,&ys);
  C2XY(scr,se,&xe,&ye);
  for (y=ys;y<=ye;++y) {
    CELL    *line=LINE(scr,y);

    x1=y==ys ? xs : 0;
    x2=y==ye ? xe : scr->width;
    for (x=x1;x<x2;++x)
      if (!(line[x].flags&CF_SELECTION))
	line[x].flags|=CF_SELECTION|CF_CHG;
  }
}

static void	RemoveSelRgn(SCREEN *scr,int ss,int se) {
  int	    xs,ys,xe,ye,x1,x2;
  int	    x,y;

  C2XY(scr,ss,&xs,&ys);
  C2XY(scr,se,&xe,&ye);
  for (y=ys;y<=ye;++y) {
    CELL    *line=LINE(scr,y);

    x1=y==ys ? xs : 0;
    x2=y==ye ? xe : scr->width;
    for (x=x1;x<x2;++x)
      if (line[x].flags&CF_SELECTION) {
	line[x].flags&=~CF_SELECTION;
	line[x].flags|=CF_CHG;
      }
  }
}

static void	RemoveSel(SCREEN *scr) {
  int	    ss,se;

  GETSEL(scr,&ss,&se);
  scr->selecting=0;
  if (ss!=se) {
    RemoveSelRgn(scr,ss,se);
    scr->vselstart=scr->vselend=0;
    RedrawWindow(hMainWnd,NULL,NULL,RDW_INTERNALPAINT);
  }
}

static void	UpdateVSel(SCREEN *scr) {
  int	  xs,ys,xe,ye,ss,se;

  if (scr->selstart==scr->selend) {
    scr->vselstart=scr->vselend=scr->selstart;
    return;
  }
  if (scr->selstart>scr->selend) {
    ss=scr->selend; se=scr->selstart;
  } else {
    ss=scr->selstart; se=scr->selend;
  }
  C2XY(scr,ss,&xs,&ys);
  C2XY(scr,se,&xe,&ye);
  if (xs>scr->vchars[ys])
    xs=scr->vchars[ys];
  if (xe>scr->vchars[ye])
    xe=scr->width;
  scr->vselstart=XY2C(scr,xs,ys);
  scr->vselend=XY2C(scr,xe,ye);
}

static void	SelStart(SCREEN *scr,int px,int py) {
  int	    x,y;

  PIX2CELL(scr,px,py,&x,&y);
  scr->selecting=1;
  scr->selstart=scr->selend=XY2C(scr,x,y);
  UpdateVSel(scr);
}

static void	ExtendSel(SCREEN *scr,int px,int py) {
  int	    x,y,ss,se,nss,nse;

  GETSEL(scr,&ss,&se);
  PIX2CELL(scr,px,py,&x,&y);
  scr->selend=XY2C(scr,x,y);
  UpdateVSel(scr);
  GETSEL(scr,&nss,&nse);
  if (nss!=ss) { /* start changed */
    if (nss<ss) /* added more points */
      MarkSelRgn(scr,nss,ss);
    else /* removed points */
      RemoveSelRgn(scr,ss,nss);
  }
  if (nse!=se) {
    if (nse<se)
      RemoveSelRgn(scr,nse,se);
    else
      MarkSelRgn(scr,se,nse);
  }
  RedrawWindow(hMainWnd,NULL,NULL,RDW_INTERNALPAINT);
}

static void	CopySel(SCREEN *scr) {
  char	    *buffer,*p;
  HGLOBAL   glob;
  int	    se,ss,xs,ys,xe,ye,x,y,x1,x2,eol;

  if (scr->selstart==scr->selend)
    return;
  GETSEL(scr,&ss,&se);
  /* allocate a large enough buffer */
  glob=GlobalAlloc(GMEM_MOVEABLE,se-ss+2*scr->height+1);
  if (!glob)
    return;
  buffer=GlobalLock(glob);
  if (!buffer) {
    GlobalFree(glob);
    return;
  }
  /* now copy chars there, we have a big enough buffer, so don't bother
     with bounds checking */
  p=buffer;
  C2XY(scr,ss,&xs,&ys);
  C2XY(scr,se,&xe,&ye);
  for (y=ys;y<=ye;++y) {
    CELL      *line=LINE(scr,y);
    x1=y==ys ? xs : 0;
    x2=y==ye ? xe : scr->width;
    if (x1>=scr->vchars[y]) {
      *p++='\r';
      *p++='\n';
      continue;
    }
    if (x2>scr->vchars[y]) {
      x2=scr->vchars[y];
      eol=1;
    } else
      eol=0;
    for (x=x1;x<x2;++x)
      *p++=line[x].ch;
    if (eol || x2>=scr->width) {
      *p++='\r';
      *p++='\n';
    }
  }
  *p++='\0';
  GlobalUnlock(glob);
  /* cool, put it into clipboard */
  if (!OpenClipboard(hMainWnd)) {
    GlobalFree(glob);
    return;
  }
  ignoredestroy=1;
  if (EmptyClipboard())
    SetClipboardData(CF_TEXT,glob);
  else
    GlobalFree(glob);
  ignoredestroy=0;
  CloseClipboard();
}

static void	SelWord(SCREEN *scr,int px,int py) {
  int	x,y,xs,xe;
  CELL	*line;

  PIX2CELL(scr,px,py,&x,&y);
  line=LINE(scr,y);
  if (isalnum(line[x].ch)) {
    for (xs=x;xs>0 && isalnum(line[xs-1].ch);--xs);
    for (xe=x;xe<scr->width && isalnum(line[xe].ch);++xe);
    scr->selstart=XY2C(scr,xs,y);
    scr->selend=XY2C(scr,xe,y);
    UpdateVSel(scr);
    GETSEL(scr,&xs,&xe);
    MarkSelRgn(scr,xs,xe);
    RedrawWindow(hMainWnd,NULL,NULL,RDW_INTERNALPAINT);
  } else
    SelStart(scr,px,py);
}

static void	FillLogFont(LOGFONT *lf) {
    char	*str;
    int		cs;

    str=regstr("Font");
    if (!str || sscanf(str,"%ld,%ld,%d,%32[^]",&lf->lfHeight, /* XXX 32 */
			      &lf->lfWeight,&cs,lf->lfFaceName)!=4)
    {
      lf->lfHeight=-16;
      lf->lfWeight=FW_BOLD;
      lf->lfCharSet=RUSSIAN_CHARSET;
      strncpy(lf->lfFaceName,"Courier New",LF_FACESIZE);
    } else
      lf->lfCharSet=cs;
    if (str)
      free(str);
    lf->lfWidth		  = 0;
    lf->lfEscapement	  = 0;
    lf->lfOrientation	  = 0;
    lf->lfItalic	  = 0;
    lf->lfUnderline	  = 0;
    lf->lfStrikeOut	  = 0;
    lf->lfOutPrecision	  = OUT_DEFAULT_PRECIS;
    lf->lfClipPrecision	  = CLIP_DEFAULT_PRECIS;
    lf->lfQuality	  = DEFAULT_QUALITY;
    lf->lfPitchAndFamily  = FIXED_PITCH | FF_DONTCARE;
}

static int	CreateStuff(void) {
    TEXTMETRIC	tm;
    HDC		hDC;
    HGDIOBJ	old;
    LOGFONT	lf;

    FillLogFont(&lf);
    hScrFont = CreateFontIndirect(&lf);
    if (!hScrFont)
      hScrFont=GetStockObject(OEM_FIXED_FONT);
    hDC=CreateDC("DISPLAY",NULL,NULL,NULL);
    old=SelectObject(hDC,hScrFont);
    GetTextMetrics(hDC,&tm);
    SelectObject(hDC,old);
    DeleteDC(hDC);
    curscr=CreateScreen(1,1,
	tm.tmAveCharWidth,
	tm.tmAscent+tm.tmDescent+tm.tmExternalLeading);
    if (!curscr)
	return 1;
    return 0;
}

static void MangleMenu(HWND hWnd) {
    hSysMenu=GetSystemMenu(hWnd,FALSE);
    AppendMenu(hSysMenu,MF_SEPARATOR,0,0);
    AppendMenu(hSysMenu,MF_STRING,CMD_CEXIT,"C&lose on exit");
    AppendMenu(hSysMenu,MF_STRING,CMD_XTERM,"Right button pastes");
    AppendMenu(hSysMenu,MF_STRING,CMD_EATMENU,"Don't open system menu by Alt key");
    AppendMenu(hSysMenu,MF_STRING,CMD_SELFONT,"&Font...");
    AppendMenu(hSysMenu,MF_STRING,CMD_SELCOLOR,"C&olors...");
    AppendMenu(hSysMenu,MF_STRING,CMD_SAVEPOS,"Save current window position and size");
#ifndef NDEBUG
    AppendMenu(hSysMenu,MF_STRING,CMD_DEBUG,"Debugging stats...");
#endif
    DrawMenuBar(hWnd); /* not sure if this is needed */
}

static LPARAM	ShiftState(void) {
  LPARAM  lp=0;

  if (GetKeyState(VK_MENU)&0x8000) /* alt */
    lp|=KF_ALT;
  if (GetKeyState(VK_CONTROL)&0x8000) /* control */
    lp|=KF_CTL;
  if (GetKeyState(VK_SHIFT)&0x8000) /* shift */
    lp|=KF_SHIFT;
  return lp;
}

static int  ProcessKey0(WPARAM key,DWORD flags) {
  if (eatmenu && (key==VK_MENU))
    return 0;
  if (flags&0x01000000) { /* ext key */
    switch (key) {
      case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_UP:
      case VK_DOWN: case VK_LEFT: case VK_RIGHT: case VK_NEXT: case VK_PRIOR:
      case VK_DIVIDE: case VK_SUBTRACT: case VK_MULTIPLY: case VK_ADD: case VK_RETURN:
	PostThreadMessage(hMainThreadID,M_KEY,key,KF_KEY|KF_PAD|ShiftState());
	break;
      default: return 1;
    }
  } else {
    switch (key) {
      case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_UP:
      case VK_DOWN: case VK_LEFT: case VK_CLEAR: case VK_RIGHT: case VK_PRIOR:
      case VK_DIVIDE: case VK_SUBTRACT: case VK_MULTIPLY: case VK_ADD: case VK_RETURN:
      case VK_NEXT: case VK_F1: case VK_F2: case VK_F3: case VK_F4: case VK_F5:
      case VK_F6: case VK_F7: case VK_F8: case VK_F9: case VK_F10: case VK_F11:
      case VK_F12:
	  PostThreadMessage(hMainThreadID,M_KEY,key,KF_KEY|ShiftState());
	  break;
      default: return 1;
    }
  }
  return 0;
}

static void   ToggleAutoclose(void) {
  if (autoclose) {
    autoclose=0;
    regprintf("CloseOnExit","%s","no");
    CheckMenuItem(hSysMenu,CMD_CEXIT,MF_BYCOMMAND|MF_UNCHECKED);
  } else {
    autoclose=1;
    regprintf("CloseOnExit","%s","yes");
    CheckMenuItem(hSysMenu,CMD_CEXIT,MF_BYCOMMAND|MF_CHECKED);
  }
}

static void   ToggleXtermbuttons(void) {
  if (xtermbuttons) {
    xtermbuttons=0;
    regprintf("XtermButtons","%s","no");
    CheckMenuItem(hSysMenu,CMD_XTERM,MF_BYCOMMAND|MF_CHECKED);
  } else {
    xtermbuttons=1;
    regprintf("XtermButtons","%s","yes");
    CheckMenuItem(hSysMenu,CMD_XTERM,MF_BYCOMMAND|MF_UNCHECKED);
  }
}

static void   ToggleEatmenu(void) {
  if (eatmenu) {
    eatmenu=0;
    regprintf("EatMenu","%s","no");
    CheckMenuItem(hSysMenu,CMD_EATMENU,MF_BYCOMMAND|MF_UNCHECKED);
  } else {
    eatmenu=1;
    regprintf("EatMenu","%s","yes");
    CheckMenuItem(hSysMenu,CMD_EATMENU,MF_BYCOMMAND|MF_CHECKED);
  }
}

static void   SelNewFont(HWND hWnd) {
  CHOOSEFONT  cf;
  LOGFONT     lf;

  memset(&cf,0,sizeof(cf));
  FillLogFont(&lf);
  cf.lStructSize	= sizeof(cf);
  cf.hwndOwner		= hWnd;
  cf.lpLogFont		= &lf;
  cf.Flags		= CF_SCREENFONTS | CF_FIXEDPITCHONLY |
			  CF_FORCEFONTEXIST | CF_INITTOLOGFONTSTRUCT;
  if (ChooseFont(&cf)) {
    HFONT     hFont=CreateFontIndirect(&lf);

    if (hFont) {
      LOCKSCR();
      DeleteObject(hScrFont);
      hScrFont=hFont;
      regprintf("Font","%ld,%ld,%d,%s",lf.lfHeight,lf.lfWeight,lf.lfCharSet,
					lf.lfFaceName);
      if (curscr) {
	TEXTMETRIC    tm;
	HGDIOBJ	      old;
	HDC	      hDC;

	hDC=GetDC(hMainWnd);
	old=SelectObject(hDC,hFont);
	GetTextMetrics(hDC,&tm);
	SelectObject(hDC,old);
	ReleaseDC(hMainWnd,hDC);
	ResizeScreen(curscr,curscr->pix_width,curscr->pix_height,
			    tm.tmAveCharWidth,
			    tm.tmAscent+tm.tmDescent+tm.tmExternalLeading,0);
	PostThreadMessage(hMainThreadID,M_RESIZE,curscr->width,curscr->height);
      }
      UNLOCKSCR();
    }
  }
}

static const char   *colornames[]={
  "ANSI Black",
  "ANSI Red",
  "ANSI Green",
  "ANSI Yellow",
  "ANSI Blue",
  "ANSI Magenta",
  "ANSI Cyan",
  "ANSI White",
  "ANSI Black Bold",
  "ANSI Red Bold",
  "ANSI Green Bold",
  "ANSI Yellow Bold",
  "ANSI Blue Bold",
  "ANSI Magenta Bold",
  "ANSI Cyan Bold",
  "ANSI White Bold"
};
#define	NAMEDCOLORS (sizeof(colornames)/sizeof(colornames[0]))

static const char   *colornames_reserved[]={
  "Cursor foreground",
  "Cursor background",
  "Selection foreground",
  "Selection background",
  "Selected cursor foreground",
  "Selected cursor background"
};

typedef struct {
  int	    cur;
  int	    chg;
  COLORREF  cmap[MAXCOLORS];
} CDLGDATA;

static void ColorsDlgApply(CDLGDATA *cd) {
  char	  buf[128];
  int	  i;

  memcpy(colormap,cd->cmap,MAXCOLORS*sizeof(COLORREF));
  RedrawWindow(hMainWnd,NULL,NULL,RDW_INVALIDATE);
  cd->chg=0;
  for (i=0;i<MAXCOLORS;++i) {
    _snprintf(buf,sizeof(buf),"Color%02x",i);
    regprintf(buf,"%d,%d,%d",GetRValue(colormap[i]),GetGValue(colormap[i]),
			     GetBValue(colormap[i]));
  }
}

LRESULT CALLBACK  ColorsDlgProc(HWND hWnd,UINT uMsg,WPARAM wParam,
				LPARAM lParam)
{
  HWND	    hItem;
  CDLGDATA  *cr;

  switch (uMsg) {
    case WM_INITDIALOG:
      SetWindowLong(hWnd,GWL_USERDATA,lParam);
      if ((hItem=GetDlgItem(hWnd,IDC_COLORSLIST))) {
	char	  buffer[128];
	int	  i;

	cr=(CDLGDATA*)lParam;
	for (i=0;i<NAMEDCOLORS;++i) {
	  _snprintf(buffer,sizeof(buffer),"%c: %s",i+'A',colornames[i]);
	  SendMessage(hItem,LB_ADDSTRING,0,(LPARAM)buffer);
	}
	for (i=NAMEDCOLORS;i<RESERVED_COLORS_START;++i) {
	  if (((i+'A')&0xff)>=' ' && ((i+'A')&0xff)<=126)
	    _snprintf(buffer,sizeof(buffer),"%c: Color %d",i+'A',i);
	  else
	    _snprintf(buffer,sizeof(buffer),"Color %d",i);
	  SendMessage(hItem,LB_ADDSTRING,0,(LPARAM)buffer);
	}
	for (i=RESERVED_COLORS_START;i<RESERVED_COLORS_END;++i)
	  SendMessage(hItem,LB_ADDSTRING,0,(LPARAM)colornames_reserved[i-RESERVED_COLORS_START]);
	for (i=RESERVED_COLORS_END;i<MAXCOLORS;++i) {
	  if (((i+'A')&0xff)>=' ' && ((i+'A')&0xff)<=126)
	    _snprintf(buffer,sizeof(buffer),"%c: Color %d",i+'A',i);
	  else
	    _snprintf(buffer,sizeof(buffer),"Color %d",i);
	  SendMessage(hItem,LB_ADDSTRING,0,(LPARAM)buffer);
	}
      }
      break;
    case WM_COMMAND:
      cr=(CDLGDATA*)GetWindowLong(hWnd,GWL_USERDATA);
      switch (LOWORD(wParam)) {
	case IDOK:
	  EndDialog(hWnd,1);
	  break;
	case IDCANCEL:
	  EndDialog(hWnd,0);
	  break;
	case IDAPPLY:
	  ColorsDlgApply(cr);
	  EnableWindow(GetDlgItem(hWnd,IDAPPLY),FALSE);
	  break;
	case IDC_COLORSLIST:
	  if (HIWORD(wParam)==LBN_SELCHANGE) {
	    int	    idx=SendMessage((HWND)lParam,LB_GETCURSEL,0,0);

	    cr->cur=idx;
	    if (idx!=LB_ERR && idx>=0 && idx<MAXCOLORS) {
	      SetDlgItemInt(hWnd,IDC_RED,GetRValue(cr->cmap[idx]),FALSE);
	      SetDlgItemInt(hWnd,IDC_GREEN,GetGValue(cr->cmap[idx]),FALSE);
	      SetDlgItemInt(hWnd,IDC_BLUE,GetBValue(cr->cmap[idx]),FALSE);
	      RedrawWindow(GetDlgItem(hWnd,IDC_COLOR),NULL,NULL,RDW_INVALIDATE);
	    }
	  }
	  break;
	case IDCCHANGE:
	  if ((hItem=GetDlgItem(hWnd,IDC_COLORSLIST))) {
	    int		    idx;

	    idx=SendMessage(hItem,LB_GETCURSEL,0,0);
	    if (idx!=LB_ERR && idx>=0 && idx<MAXCOLORS) {
	      CHOOSECOLOR     cc;
	      static COLORREF cust_colors[16];

	      cr->cur=idx;
	      memset(&cc,0,sizeof(cc));
	      cc.lStructSize=sizeof(cc);
	      cc.hwndOwner=hWnd;
	      cc.rgbResult=cr->cmap[idx];
	      cc.lpCustColors=cust_colors;
	      cc.Flags=CC_FULLOPEN|CC_SOLIDCOLOR|CC_RGBINIT;
	      if (ChooseColor(&cc)) {
		cr->cmap[idx]=cc.rgbResult;
		cr->chg=1;
		EnableWindow(GetDlgItem(hWnd,IDAPPLY),TRUE);
		SetDlgItemInt(hWnd,IDC_RED,GetRValue(cr->cmap[idx]),FALSE);
		SetDlgItemInt(hWnd,IDC_GREEN,GetGValue(cr->cmap[idx]),FALSE);
		SetDlgItemInt(hWnd,IDC_BLUE,GetBValue(cr->cmap[idx]),FALSE);
		RedrawWindow(GetDlgItem(hWnd,IDC_COLOR),NULL,NULL,
							RDW_INVALIDATE);
	      }
	    }
	  }
	  break;
      }
      break;
    case WM_DRAWITEM: {
      if (wParam==IDC_COLOR) {
	LPDRAWITEMSTRUCT  id=(LPDRAWITEMSTRUCT)lParam;
	HBRUSH		  hbr;
    
	cr=(CDLGDATA*)GetWindowLong(hWnd,GWL_USERDATA);
	hbr=CreateSolidBrush(cr->cmap[cr->cur]);
	FillRect(id->hDC,&id->rcItem,hbr);
	DeleteObject(hbr);
	break;
      }
    }
    default:
      return FALSE;
  }
  return TRUE;
}

static void   SelColors(HWND hWnd) {
  CDLGDATA    cd;

  cd.cur=0;
  cd.chg=0;
  memcpy(cd.cmap,colormap,MAXCOLORS*sizeof(COLORREF));
  if (DialogBoxParam(g_hInst,MAKEINTRESOURCE(IDD_COLORS),hWnd,
		      (DLGPROC)ColorsDlgProc,(LPARAM)&cd) && cd.chg)
    ColorsDlgApply(&cd);
}

static void   SaveWindowPos(HWND hWnd) {
  WINDOWPLACEMENT pl;

  memset(&pl,0,sizeof(pl));
  pl.length=sizeof(pl);
  GetWindowPlacement(hWnd,&pl);
  regprintf("WindowPos","%u,%u,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld",
			pl.flags,
			pl.showCmd,
			pl.ptMinPosition.x,
			pl.ptMinPosition.y,
			pl.ptMaxPosition.x,
			pl.ptMaxPosition.y,
			pl.rcNormalPosition.left,
			pl.rcNormalPosition.top,
			pl.rcNormalPosition.right,
			pl.rcNormalPosition.bottom);
}

LRESULT CALLBACK    MainWndProc(HWND hWnd,UINT uMsg,
			    WPARAM wParam,LPARAM lParam)
{
    RECT	    rc;

    switch (uMsg) {
    case WM_CREATE: {
	char	*str;
	int	i,r,g,b;
	char	buf[128];

	MangleMenu(hWnd);
	if ((str=regstr("CloseOnExit"))) {
	  if (str[0]!='n')
	    autoclose=1;
	  free(str);
	} else
	  autoclose=1;
	if (autoclose)
	  CheckMenuItem(hSysMenu,CMD_CEXIT,MF_BYCOMMAND|MF_CHECKED);
	if ((str=regstr("XtermButtons"))) {
	  if (str[0]=='y') {
	    xtermbuttons=1;
	  }
	  free(str);
	}
	if (!xtermbuttons)
	  CheckMenuItem(hSysMenu,CMD_XTERM,MF_BYCOMMAND|MF_CHECKED);
	if ((str=regstr("EatMenu"))) {
	  if (str[0]=='y') {
	    eatmenu=1;
	  }
	  free(str);
	}
	if (eatmenu)
	  CheckMenuItem(hSysMenu,CMD_EATMENU,MF_BYCOMMAND|MF_CHECKED);
	colormap[CURSOR_FG]=RGB(0,0,0); /* cursor fg */
	colormap[CURSOR_BG]=RGB(0,255,0); /* cursor bg */
	colormap[SELECTED_FG]=RGB(0,0,0); /* selection fg */
	colormap[SELECTED_BG]=RGB(192,192,192); /* selection bg */
	colormap[SELECTED_CURSOR_FG]=RGB(0,0,0); /* selected cursor fg */
	colormap[SELECTED_CURSOR_BG]=RGB(0,255,0); /* selected cursor bg */
	for (i=0;i<MAXCOLORS;++i) {
	  _snprintf(buf,sizeof(buf),"Color%02x",i);
	  if ((str=regstr(buf))) {
	    if (sscanf(str,"%d,%d,%d",&r,&g,&b)==3)
	      colormap[i]=RGB(r,g,b);
	    free(str);
	  }
	}
	break;
    }
    case WM_PAINT: {
	PAINTSTRUCT ps;
	HDC	    hDC;
	HRGN	    hPr;

	hPr=CreateRectRgn(0,0,0,0);
	GetClientRect(hWnd,&rc);
	if (GetUpdateRgn(hWnd,hPr,FALSE)!=NULLREGION) { /* normal paint */
	  hDC=BeginPaint(hWnd,&ps);
	  LOCKSCR();
	  if (ps.fErase)
	    PadClear(hDC,&ps.rcPaint,&rc);
	  PaintWindow(hDC,hPr,&rc);
	  UNLOCKSCR();
	  EndPaint(hWnd,&ps);
	} else { /* internal paint */
	  hDC=GetDC(hWnd);
	  LOCKSCR();
	  PaintWindow(hDC,NULL,&rc);
	  UNLOCKSCR();
	  ReleaseDC(hWnd,hDC);
	}
	DeleteObject(hPr);
	break;
    }
    case WM_SIZE:
	LOCKSCR();
	if (curscr) {
	    if (wParam!=SIZE_MINIMIZED &&
		(curscr->pix_width!=LOWORD(lParam) ||
		 curscr->pix_height!=HIWORD(lParam)))
	    {
	      ResizeScreen(curscr,LOWORD(lParam),HIWORD(lParam),
		  curscr->ch_width,curscr->ch_height,0);
	      PostThreadMessage(hMainThreadID,M_RESIZE,
		curscr->width,curscr->height);
	    }
	}
	UNLOCKSCR();
	SetEvent(hSizeEvent);
	break;
    case WM_CLOSE:
	PostThreadMessage(hMainThreadID,M_CLOSE,0,0); /* notify main loop */
	autoclose=1;
	if (!bibi)
	  break;
    case M_CLOSE: /* deferred close */
	DestroyWindow(hWnd);
	break;
    case M_BYEBYE:
	SetWindowText(hWnd,"MMC [Finished]");
	bibi=1;
	break;
    case WM_SYSCHAR: /* eat these too */
    case WM_CHAR:
      if (bibi)
	goto def;
      PostThreadMessage(hMainThreadID,M_KEY,wParam,ShiftState());
      break;
    case WM_SYSKEYDOWN: /* eat these too */
    case WM_KEYDOWN:
      if (bibi)
	goto def;
      if (ProcessKey0(wParam,lParam))
	goto def;
      break;
    case WM_LBUTTONDOWN:
      LOCKSCR();
      if (curscr) {
	RemoveSel(curscr);
	curscr->selecting=1;
	SelStart(curscr,GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
	SetCapture(hWnd);
      }
      UNLOCKSCR();
      break;
    case WM_LBUTTONUP:
      ReleaseCapture();
      LOCKSCR();
      if (curscr && curscr->selecting) {
	/*ExtendSel(curscr,GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));*/
	curscr->selecting=0;
	CopySel(curscr);
      }
      UNLOCKSCR();
      break;
    case WM_RBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
      if (xtermbuttons) {
  extend:
	LOCKSCR();
	if (curscr && !curscr->selecting) {
	  ExtendSel(curscr,GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
	  if (!curscr->selecting)
	    CopySel(curscr);
	}
	UNLOCKSCR();
      } else
	PostThreadMessage(hMainThreadID,M_PASTE,0,0);
      break;
    case WM_MBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
      if (xtermbuttons)
	PostThreadMessage(hMainThreadID,M_PASTE,0,0);
      else
	goto extend;
      break;
    case WM_LBUTTONDBLCLK:
      LOCKSCR();
      if (curscr) {
	RemoveSel(curscr);
	SelWord(curscr,GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
	curscr->selecting=1;
	SetCapture(hWnd);
      }
      UNLOCKSCR();
      break;
    case WM_MOUSEMOVE:
      LOCKSCR();
      if (curscr && curscr->selecting)
	ExtendSel(curscr,GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
      UNLOCKSCR();
      break;
    case WM_GETMINMAXINFO: {
      LPMINMAXINFO  mi=(LPMINMAXINFO)lParam;
      LOCKSCR();
      if (curscr) {
	mi->ptMinTrackSize.x=curscr->ch_width*20+XLEFTPAD;
	mi->ptMinTrackSize.y=curscr->ch_height*4+GetSystemMetrics(SM_CYCAPTION);
      } else {
	mi->ptMinTrackSize.x=160;
	mi->ptMinTrackSize.y=64+GetSystemMetrics(SM_CYCAPTION);
      }
      UNLOCKSCR();
      break;
    }
    case WM_SYSCOMMAND:
      switch (wParam) {
	case CMD_CEXIT:
	  ToggleAutoclose();
	  break;
	case CMD_XTERM:
	  ToggleXtermbuttons();
	  break;
	case CMD_EATMENU:
	  ToggleEatmenu();
	  break;
	case CMD_SELFONT:
	  SelNewFont(hWnd);
	  break;
	case CMD_SELCOLOR:
	  SelColors(hWnd);
	  break;
	case CMD_SAVEPOS:
	  SaveWindowPos(hWnd);
	  break;
#ifndef NDEBUG
	case CMD_DEBUG:
	  disp_paint_times();
	  break;
#endif
	default:
	  goto def;
      }
      break;
    case WM_DESTROYCLIPBOARD:
      if (!ignoredestroy) {
	LOCKSCR();
	if (curscr)
	  RemoveSel(curscr);
	UNLOCKSCR();
      }
      break;
    case WM_DESTROY:
	PostQuitMessage(0);
	break;
    default: def:
	return DefWindowProc(hWnd,uMsg,wParam,lParam);
    }
    return 0;
}

static int  RegClass(HINSTANCE hInst) {
    WNDCLASSEX	    wc;

    wc.cbSize		    = sizeof(wc);
    wc.style		    = CS_DBLCLKS;
    wc.lpfnWndProc	    = MainWndProc;
    wc.cbClsExtra	    = 0;
    wc.cbWndExtra	    = 0;
    wc.hInstance	    = hInst;
    wc.hIcon		    = LoadIcon(NULL,IDI_APPLICATION);
    wc.hCursor		    = LoadCursor(NULL,IDC_IBEAM);
    wc.hbrBackground	    = NULL;
    wc.lpszMenuName	    = NULL;
    wc.lpszClassName	    = "MMC";
    wc.hIconSm		    = LoadIcon(NULL,IDI_APPLICATION);

    return !RegisterClassEx(&wc);
}

static DWORD WINAPI	MainThread(LPVOID arg) {
  MSG		  msg;
  char		  *str;
  int		  show=1;
  WINDOWPLACEMENT pl;

  hMainWnd=CreateWindow("MMC","MMC",WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,
      NULL,NULL,g_hInst,0);
  if (!hMainWnd)
      exit(1);
  if ((str=regstr("WindowPos"))) {
    memset(&pl,0,sizeof(pl));
    pl.length=sizeof(pl);
    if (sscanf(str,"%u,%u,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld",
		    &pl.flags,
		    &pl.showCmd,
		    &pl.ptMinPosition.x,
		    &pl.ptMinPosition.y,
		    &pl.ptMaxPosition.x,
		    &pl.ptMaxPosition.y,
		    &pl.rcNormalPosition.left,
		    &pl.rcNormalPosition.top,
		    &pl.rcNormalPosition.right,
		    &pl.rcNormalPosition.bottom)==10)
    {
      SetWindowPlacement(hMainWnd,&pl);
      show=0;
    }
    free(str);
  }
  if (show)
    ShowWindow(hMainWnd,(int)arg);
  InvalidateRect(hMainWnd,NULL,FALSE);
  UpdateWindow(hMainWnd);
  while (GetMessage(&msg,NULL,0,0)) {
    /* do some checks to prevent it from generating wm_chars for keypad */
    if (msg.message==WM_KEYDOWN || msg.message==WM_KEYUP)
      switch (msg.wParam) {
	case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_UP:
	case VK_DOWN: case VK_LEFT: case VK_RIGHT: case VK_NEXT: case VK_PRIOR:
	case VK_CLEAR: case VK_SUBTRACT: case VK_MULTIPLY: case VK_DIVIDE: case VK_ADD:
	case VK_RETURN:
	  goto notrans;
      }
    TranslateMessage(&msg);
  notrans:
    DispatchMessage(&msg);
  }
  return 0;
}

static int  CreateWnd(HINSTANCE hInst,int nCmdShow) {
  DWORD	    id;

  g_hInst=hInst;
  hScrMutex=CreateMutex(NULL,FALSE,NULL);
  if (!hScrMutex)
    return 0;
  hSizeEvent=CreateEvent(NULL,FALSE,FALSE,NULL);
  if (!hSizeEvent)
    return 0;
  hMainThreadID=GetCurrentThreadId();
  if (CreateStuff()) /* screen creation failed */
    return 1;
  hGUIThread=CreateThread(NULL,0,&MainThread,(LPVOID)nCmdShow,0,&id);
  if (!hGUIThread)
    return 1;
  return 0;
}


extern int	wmain(int argc,char **argv,char **envp);

int APIENTRY WinMain(HINSTANCE hInstance,
		     HINSTANCE hPrevInstance,
		     LPSTR     lpCmdLine,
		     int       nCmdShow)
{
    char    *env[1]={NULL};
    char    exefile[1024],*p;
    char    *args[3]={exefile,NULL,NULL};
    MSG	    msg;
    DWORD   n;
    int	    ret;

    if (RegCreateKey(HKEY_CURRENT_USER,REGPATH,&hAppKey)!=ERROR_SUCCESS)
      hAppKey=NULL;
    if (RegClass(hInstance))
      return 0;
    if (CreateWnd(hInstance,nCmdShow))
      return 0;
    /* force the system to create a message queue for us */
    PeekMessage(&msg,NULL,WM_USER,WM_USER,PM_NOREMOVE);
    /* wait for the main window to appear */
    WaitForSingleObject(hSizeEvent,300); /* up to 0.3s */
    n=GetModuleFileName(hInstance,exefile,sizeof(exefile));
    if (n<sizeof(exefile)) {
      exefile[n]='\0';
      for (p=exefile;*p;++p)
	if (*p=='\\')
	  *p='/';
    } else
      strcpy(exefile,"mmc");
    args[1]=lpCmdLine;
    ret=wmain(2,args,env); /* kick in our regular stuff */
    flush_socks();
    window_flush(); /* force window flush in case we are exiting due to an error */
    /* don't close window when there are errors */
    if (autoclose && ret==0 && loop_finished()) {
      PostMessage(hMainWnd,M_CLOSE,0,0);
      WaitForSingleObject(hGUIThread,3000); /* give it up to 3 seconds */
    } else {
      PostMessage(hMainWnd,M_BYEBYE,0,0);
      WaitForSingleObject(hGUIThread,INFINITE); /* wait until user closes it */
    }
    CloseHandle(hGUIThread);
    CloseHandle(hScrMutex);
    DeleteObject(hScrFont);
    if (hAppKey)
      RegCloseKey(hAppKey);
    exit(0);
    return 0;
}

/* here we implement output interface */
void	out_addkey(const char *key,const char *name) {
  /* unimplemented here */
}

void	out_setcolor(int fg,int bg) {
    LOCKSCR();
    if (!curscr) {
	UNLOCKSCR();
	return;
    }
    if (fg>=0 && fg<256) /* we support up to 256 on win32 */
	curscr->fg=fg;
    if (bg<0) /* default color is 0 on win32 */
	bg=0;
    if (bg<256)
	curscr->bg=bg;
    UNLOCKSCR();
}

void	out_cwrite(const char *ctext,int len,int ibg) {
    CELL	    *line;
    int		    i;
    unsigned char   bg;

    if (ibg<0)
      ibg=0;
    LOCKSCR();
    if (!curscr)
      goto out;
    if (len>curscr->width-curscr->cursorx)
	len=curscr->width-curscr->cursorx;
    if (len<=0)
	goto out;
    if (INSELXYL(curscr,curscr->cursorx,curscr->cursory,len))
      RemoveSel(curscr);
    bg=ibg;
    line=LINE(curscr,curscr->cursory)+curscr->cursorx;
    for (i=0;i<len;++i) {
	if (line[i].ch!=ctext[i<<1] ||
	    line[i].fg!=ctext[(i<<1)+1] ||
	    line[i].bg!=bg) {
	    line[i].ch=ctext[i<<1];
	    line[i].fg=ctext[(i<<1)+1];
	    line[i].bg=bg;
	    line[i].flags|=CF_CHG;
	}
    }
    curscr->cursorx+=len;
    curscr->vchars[curscr->cursory]=curscr->cursorx;
    if (curscr->cursorx>=curscr->width)
	curscr->cursorx=curscr->width-1;
out:
    UNLOCKSCR();
}

void	out_rawrite(const char *text,int len) {
    CELL	    *line;
    int		    i;
    unsigned char   fg,bg;

    LOCKSCR();
    if (!curscr)
	goto out;
    if (len>curscr->width-curscr->cursorx)
	len=curscr->width-curscr->cursorx;
    if (len<=0)
	goto out;
    if (INSELXYL(curscr,curscr->cursorx,curscr->cursory,len))
      RemoveSel(curscr);
    fg=curscr->fg;
    bg=curscr->bg;
    line=LINE(curscr,curscr->cursory)+curscr->cursorx;
    for (i=0;i<len;++i) {
	if (line[i].ch!=text[i] ||
	    line[i].fg!=fg ||
	    line[i].bg!=bg) {
	    line[i].ch=text[i];
	    line[i].fg=fg;
	    line[i].bg=bg;
	    line[i].flags|=CF_CHG;
	}
    }
    curscr->cursorx+=len;
    curscr->vchars[curscr->cursory]=curscr->cursorx;
    if (curscr->cursorx>=curscr->width)
	curscr->cursorx=curscr->width-1;
out:
    UNLOCKSCR();
}

void	out_inschars(int n) {
    int		    i,l;
    CELL	    *line;
    unsigned char   fg,bg;

    LOCKSCR();
    if (!curscr)
	goto out;
    if (n>curscr->width-curscr->cursorx)
	n=curscr->width-curscr->cursorx;
    if (n<=0)
	goto out;
    if (INSELXYL(curscr,curscr->cursorx,curscr->cursory,curscr->width-curscr->cursorx))
      RemoveSel(curscr);
    /* copy what needs to be saved */
    line=LINE(curscr,curscr->cursory)+curscr->cursorx;
    l=curscr->width-curscr->cursorx-n;
    for (i=l+n-1;i>=n;--i) {
	if (!CEQ(line+i,line+i-n)) {
	    line[i]=line[i-n];
	    line[i].flags|=CF_CHG;
	    line[i].flags&=~CF_CURSOR; /* hehe, don't copy cursors */
	}
    }
    /* fill in new cells */
    fg=curscr->fg;
    bg=curscr->bg;
    for (i=0;i<n;++i)
	if (line[i].ch!=' ' || line[i].bg!=bg) {
	    line[i].ch=' ';
	    line[i].fg=fg;
	    line[i].bg=bg;
	    line[i].flags|=CF_CHG;
	}
    if (curscr->vchars[curscr->cursory]>curscr->cursorx) {
      if ((curscr->vchars[curscr->cursory]+=n)>curscr->width)
	curscr->vchars[curscr->cursory]=curscr->width;
    }
out:
    UNLOCKSCR();
}

void	out_delchars(int n) {
    int		    i,l;
    CELL	    *line;
    unsigned char   fg,bg;

    LOCKSCR();
    if (!curscr)
	goto out;
    if (n>curscr->width-curscr->cursorx)
	n=curscr->width-curscr->cursorx;
    if (n<=0)
	goto out;
    if (INSELXYL(curscr,curscr->cursorx,curscr->cursory,curscr->width-curscr->cursorx))
      RemoveSel(curscr);
    /* copy what needs to be saved */
    l=curscr->width-curscr->cursorx-n;
    line=LINE(curscr,curscr->cursory)+curscr->cursorx;
    for (i=0;i<l;++i) {
	if (!CEQ(line+i,line+n+i)) {
	    line[i]=line[n+i];
	    line[i].flags|=CF_CHG;
	    line[i].flags&=~CF_CURSOR; /* hehe, don't copy cursors */
	}
    }
    /* fill in new cells */
    line+=l;
    fg=curscr->fg;
    bg=curscr->bg;
    for (i=0;i<n;++i)
	if (line[i].ch!=' ' || line[i].bg!=bg) {
	    line[i].ch=' ';
	    line[i].fg=fg;
	    line[i].bg=bg;
	    line[i].flags|=CF_CHG;
	}
    if (curscr->vchars[curscr->cursory]>curscr->cursorx) {
      if ((curscr->vchars[curscr->cursory]-=n)<curscr->cursorx)
	curscr->vchars[curscr->cursory]=curscr->cursorx;
    }
out:
    UNLOCKSCR();
}


void	out_setscroll(int top,int bot) {
    LOCKSCR();
    if (!curscr)
	goto out;
    if (top<0 || bot>=curscr->height || top>bot)
      goto out;
    curscr->srtop=top;
    curscr->srbot=bot;
out:
    UNLOCKSCR();
}

void	out_movecursor(int x,int y) {
    LOCKSCR();
    if (!curscr || x<0 || x>=curscr->width || y<0 || y>=curscr->height)
	goto out;
    curscr->cursorx=x;
    curscr->cursory=y;
out:
    UNLOCKSCR();
}

void	out_clearscr(void) { /* clear entire screen */
    unsigned char   fg,bg;
    int		    x,y;

    RemoveSel(curscr);
    LOCKSCR();
    if (!curscr)
	goto out;
    fg=curscr->fg;
    bg=curscr->bg;
    for (y=0;y<curscr->height;++y) {
	CELL	*line=LINE(curscr,y);
	for (x=0;x<curscr->width;++x) {
	    line[x].ch=' ';
	    line[x].fg=fg;
	    line[x].bg=bg;
	    line[x].flags=CF_CHG;
	}
	curscr->vchars[y]=0;
    }
out:
    UNLOCKSCR();
}

void	out_clearline(void) { /* clear to end of line */
    int		    i,n;
    CELL	    *line;
    unsigned char   fg,bg;

    LOCKSCR();
    if (!curscr)
	goto out;
    n=curscr->width-curscr->cursorx;
    if (INSELXYL(curscr,curscr->cursorx,curscr->cursory,n))
      RemoveSel(curscr);
    line=LINE(curscr,curscr->cursory)+curscr->cursorx;
    fg=curscr->fg;
    bg=curscr->bg;
    for (i=0;i<n;++i)
	if (line[i].ch!=' ' || line[i].fg!=fg || line[i].bg!=bg) {
	    line[i].ch=' ';
	    line[i].fg=fg;
	    line[i].bg=bg;
	    line[i].flags|=CF_CHG;
	}
    if (curscr->vchars[curscr->cursory]>curscr->cursorx)
      curscr->vchars[curscr->cursory]=curscr->cursorx;
out:
    UNLOCKSCR();
}

void	out_update_winsize(void) {
    LOCKSCR();
    if (!curscr || !initdone)
	goto out;
    window_resize(curscr->width,curscr->height);
out:
    UNLOCKSCR();
}

void out_drawlines(void *w,int count,int ibg) {
    int		    save,top,bot;
    int		    x,y,xe;
    unsigned char   fg,bg;
    RECT	    rcclip;

    if (ibg<0)
      ibg=0; /* default color is 0 on win32 */
    LOCKSCR();
    top=curscr->srtop;
    bot=curscr->srbot;
    if (INSELY(curscr,top,bot-top+1))
      RemoveSel(curscr);
    if (count>bot-top+1) /* adjust count */
	count=bot-top+1;
    save=bot-top+1-count;
    if (save) {
      HRGN    hRgnScroll;

      /* remove cursor if needed */
      if (curscr->vcursory>=top && curscr->vcursory<=bot)
	LINE(curscr,curscr->vcursory)[curscr->vcursorx].flags&=~CF_CURSOR;
      /* copy scrolled lines */
      memmove(LINE(curscr,top),LINE(curscr,top+count),save*curscr->width*sizeof(CELL));
      memmove(curscr->vchars+top,curscr->vchars+top+count,save*sizeof(int));
      /* XXX calling GDI from a different thread, currently that is protected */
      /* by hScrMutex, but still need to take care */
      GetClientRect(hMainWnd,&rcclip);
      rcclip.top+=curscr->pady+top*curscr->ch_height;
      rcclip.bottom=rcclip.top+(bot-top+1)*curscr->ch_height;
      hRgnScroll=CreateRectRgn(0,0,0,0);
      ScrollWindowEx(hMainWnd,0,-count*curscr->ch_height,&rcclip,&rcclip,hRgnScroll,NULL,0);
      GetClientRect(hMainWnd,&rcclip);
      MarkRgnChg(hRgnScroll,&rcclip);
      DeleteObject(hRgnScroll);
    } else /* mark the entire region as changed */
      for (x=top*curscr->width,y=(bot+1)*curscr->width;x<y;++x)
	curscr->data[x].flags|=CF_CHG;
    /* ok, scrolled the area, now fill in fresh lines */
    bg=ibg;
    fg=curscr->fg;
    for (y=top+save;y<=bot;++y) {
	CELL		*line=LINE(curscr,y);
	const char	*ctext;
	int		len;

	if (window_getline((struct Window *)w,bot-y,&ctext,&len)>=0) {
	  if (len>curscr->width)
	    len=curscr->width;
	  for (x=0;x<len;++x) {
	    line[x].ch=ctext[x<<1];
	    line[x].fg=ctext[(x<<1)+1];
	    line[x].bg=bg;
	  }
	} else
	    x=0;
	curscr->vchars[y]=x;
	/* find the last non-space on the line */
	for (xe=curscr->width;xe>x;--xe)
	  if (line[xe-1].ch!=' ' || line[xe-1].bg!=bg)
	    break;
	/* and erase everything up to it */
	for (;x<xe;++x) {
	  line[x].ch=' ';
	  line[x].bg=bg;
	  line[x].fg=fg;
	}
    }
    UNLOCKSCR();
}

static void PasteClip(void) {
  HGLOBAL	glob;
  unsigned char	*clip,*buf,*p,*q;
  int		len;

  if (!OpenClipboard(NULL))
    return;
  buf=NULL;
  if (IsClipboardFormatAvailable(CF_TEXT) &&
      (glob=GetClipboardData(CF_TEXT)) &&
      (clip=GlobalLock(glob)))
  {
    len=strlen(clip);
    if ((buf=malloc(len+1))) {
      for (p=clip,q=buf;*p;++p)
	if (*p=='\n' || *p>=0x20)
	  *q++=*p;
      *q='\0';
    }
    GlobalUnlock(glob);
  }
  CloseClipboard();
  if (buf) {
    for (p=buf;*p;++p)
      if (*p=='\n')
	window_process_key("C-M",3);
      else
	window_process_key(p,1);
    free(buf);
  }
}

static void addmod(const char *kn,int l,int flags) {
  char	  buf[32];
  char	  *p=buf,*e=buf+sizeof(buf);

  if (l<0)
    l=strlen(kn);
  if (l==0)
    return;
  if ((flags&KF_SHIFT) && l>1) { /* shift */
    *p++='S';
    *p++='-';
  }
  if (flags&KF_ALT) { /* alt */
    *p++='M';
    *p++='-';
  }
  if (flags&KF_CTL) { /* control */
    *p++='C';
    *p++='-';
  }
  while (l-- && p<e)
    *p++=*kn++;
  window_process_key(buf,p-buf);
}

static int  ProcessKey1(WPARAM key,LPARAM flags) {
  if (flags&KF_PAD) { /* ext key */
    switch (key) {
      case VK_INSERT:
	if ((flags&(KF_SHIFT|KF_ALT|KF_CTL))==KF_SHIFT)
	  PasteClip();
	else
	  addmod("ins",3,flags);
	break;
      case VK_DELETE: addmod("del",3,flags); break;
      case VK_HOME: addmod("home",4,flags); break;
      case VK_END: addmod("end",3,flags); break;
      case VK_UP: addmod("up",2,flags); break;
      case VK_DOWN: addmod("down",4,flags); break;
      case VK_LEFT: addmod("left",4,flags); break;
      case VK_RIGHT: addmod("right",5,flags); break;
      case VK_NEXT: addmod("pgdn",4,flags); break;
      case VK_PRIOR: addmod("pgup",4,flags); break;
      case VK_ADD: addmod("k+",2,flags); break;
      case VK_MULTIPLY: addmod("k*",2,flags); break;
      case VK_SUBTRACT: addmod("k-",2,flags); break;
      case VK_DIVIDE: addmod("k/",2,flags); break;
      case VK_RETURN: addmod("C-M",3,flags&~KF_CTL); break;
    }
  } else {
    if (flags&KF_KEY)
      switch (key) {
      case VK_INSERT:
	if ((flags&(KF_SHIFT|KF_ALT|KF_CTL))==KF_SHIFT)
	  PasteClip();
	else
	  addmod("kins",4,flags);
	break;
	case VK_DELETE: addmod("kdel",4,flags); break;
	case VK_HOME: addmod("k7",2,flags); break;
	case VK_END: addmod("k1",2,flags); break;
	case VK_UP: addmod("k8",2,flags); break;
	case VK_DOWN: addmod("k2",2,flags); break;
	case VK_LEFT: addmod("k4",2,flags); break;
	case VK_CLEAR: addmod("k5",2,flags); break;
	case VK_RIGHT: addmod("k6",2,flags); break;
	case VK_PRIOR: addmod("k9",2,flags); break;
	case VK_NEXT: addmod("k3",2,flags); break;
	case VK_ADD: addmod("k+",2,flags); break;
	case VK_MULTIPLY: addmod("k*",2,flags); break;
	case VK_SUBTRACT: addmod("k-",2,flags); break;
	case VK_DIVIDE: addmod("k/",2,flags); break;
	case VK_RETURN: addmod("C-M",3,flags&~KF_CTL); break;
	case VK_F1: addmod("f1",2,flags); break;
	case VK_F2: addmod("f2",2,flags); break;
	case VK_F3: addmod("f3",2,flags); break;
	case VK_F4: addmod("f4",2,flags); break;
	case VK_F5: addmod("f5",2,flags); break;
	case VK_F6: addmod("f6",2,flags); break;
	case VK_F7: addmod("f7",2,flags); break;
	case VK_F8: addmod("f8",2,flags); break;
	case VK_F9: addmod("f9",2,flags); break;
	case VK_F10: addmod("f10",3,flags); break;
	case VK_F11: addmod("f11",3,flags); break;
	case VK_F12: addmod("f12",3,flags); break;
      }
    else { /* this is a char */
      if (key<0x20) {
	char	ch[3];
	ch[0]='C';
	ch[1]='-';
	ch[2]=key+'@';
	flags&=~KF_CTL;
	addmod(ch,3,flags);
      } else {
	char	ch=key;
	addmod(&ch,1,flags);
      }
    }
  }
  return 0;
}

void	out_process_input(void) {
    MSG	    msg;

    /* we don't have any windows in this thread, so process messages here */
    while (PeekMessage(&msg,NULL,0,0,PM_REMOVE))
      switch (msg.message) {
	case M_KEY:
	  ProcessKey1(msg.wParam,msg.lParam);
	  break;
	case M_RESIZE:
	  LOCKSCR();
	  window_resize(curscr->width,curscr->height);
	  UNLOCKSCR();
	  nextfulldraw=1;
	  break;
	case M_CLOSE:
	  post_quit_message();
	  break;
	case M_PASTE:
	  PasteClip();
	  break;
	case M_PIPEDATA:
	  if (msg.lParam) {
	    soproc(msg.wParam,((struct pipedata *)msg.lParam)->buf,
		((struct pipedata *)msg.lParam)->size);
	    free((void*)msg.lParam);
	  } else {
	    char tmp;
	    soproc(msg.wParam,&tmp,0);
	  }
	  break;
	default:
	  break;
    }
}

const char *out_setup(int how) {
    /* out_setup does nothing here, this work happens in WinMain */
    initdone=how;
    return NULL;
}

void	out_sigcheck(void) {
    /* resize check does nothing on win32 */
}

void	out_playsound(const char *name) {
  if (!name || !name[0]) { /* play a default sound */
    Beep(800,50);
  } else {
    PlaySound(name,NULL,SND_ASYNC|SND_FILENAME|SND_NODEFAULT);
  }
}

/* seems that posting messages _does_ _not_ _work_ reliably, so all
   updating will happen here directly */
void	out_flush(void) {
  if (nextfulldraw) {
    /* this can be deferred, since the whole window will be repainted anyway */
    RedrawWindow(hMainWnd,NULL,NULL,RDW_INVALIDATE);
    nextfulldraw=0;
  } else
    /* but this must always happen here */
    RedrawWindow(hMainWnd,NULL,NULL,RDW_INTERNALPAINT|RDW_UPDATENOW);
}
