#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>

#include "misc.h"
#include "window.h"

void	print_message(int type,const char *errmsg,const char *fmt) {
    Char    buffer[2048];
    int	    attr;
    int	    i=4;

    buffer[0]=MKCH('-',9); buffer[1]=MKCH(':',10);
    buffer[2]=MKCH('-',9); buffer[3]=MKCH(' ',9);
    switch (type) {
	case 2:
	    attr=9;
	    break;
	case 1:
	    attr=1;
	    break;
	default:
	    attr=7;
	    break;
    }
    while (i<2048 && *fmt)
      buffer[i++]=MKCH(*fmt++,attr);
    if (errmsg) {
	if (i<2048-2) {
	  buffer[i++]=MKCH(':',attr);
	  buffer[i++]=MKCH(' ',attr);
	}
      while (i<2048 && *errmsg)
        buffer[i++]=MKCH(*errmsg++,attr);
    }
    window_output_ctext(0,0,(char*)buffer,i);
}

void	strip_colors(const Char *src,int srclen,char *dest,int *destlen) {
    char	*p=dest;
    char	*e=dest+*destlen;
    const Char	*q=src+srclen;

    while (src<q && p<e)
	    *p++=CH(*src++);
    *destlen=p-dest;
}

void	parse_colors(const char *src,int srclen,Char *dest,int *destlen,int attr) {
  Char		*p=dest;
  Char		*e=dest+*destlen;
  const char	*q=src+srclen;

  while (src<q && p<e) {
    if (*src=='\003') {
      ++src;
      if (src<q) {
	attr=(unsigned char)(*src-'A');
	++src;
      }
    } else
      *p++=MKCH(*src++,attr);
  }
  *destlen=p-dest;
}

void	unparse_colors(const Char *src,int srclen,char *dest,int *destlen) {
  char	      *p=dest;
  char	      *e=dest+*destlen-3;
  const Char  *q=src+srclen;
  int	      color=-1;

  if (*destlen<3) {
    *destlen=0;
    return;
  }
  while (src<q && p<e) {
    if (COL(*src)!=color) {
      color=COL(*src);
      *p++='\003';
      *p++='A'+(color&0xff);
    }
    *p++=CH(*src);
    ++src;
  }
  *destlen=p-dest;
}

static const char	*errstr(void) {
	return strerror(errno);
}

void	clmsg(const char *fmt) {
	print_message(MMSG,NULL,fmt);
}

void	clwarn(const char *fmt) {
	print_message(MWARN,errstr(),fmt);
}

void	clwarnx(const char *fmt) {
	print_message(MWARN,NULL,fmt);
}

void	clerr(const char *fmt) {
	print_message(MERR,errstr(),fmt);
}

void	clerrx(const char *fmt) {
	print_message(MERR,NULL,fmt);
}

int	streq(const char *s1,int s1len,const char *s2) {
  int	s2len=strlen(s2);
  if (s2len!=s1len)
    return 0;
  if (memcmp(s1,s2,s1len))
    return 0;
  return 1;
}
