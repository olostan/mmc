#ifndef MISC_H
#define MISC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Char.h"

typedef	void	(*PTHANDLER)(int);

PTHANDLER   set_term_handler(PTHANDLER h);

void	strip_colors(const Char *src,int srclen,char *dest,int *destlen);
void	parse_colors(const char *src,int srclen,Char *dest,int *destlen,int attr);
void	unparse_colors(const Char *src,int srclen,char *dest,int *destlen);

#define	MMSG	0
#define	MWARN	1
#define	MERR	2

void	print_message(int type,const char *errmsg,const char *fmt);

void	clmsg(const char *fmt);
void	clwarn(const char *fmt);
void	clwarnx(const char *fmt);
void	clerr(const char *fmt);
void	clerrx(const char *fmt);

int	get_packed_module_data(int idx,const char **name,const char **pdata,
				int *osize,int *psize);

const char *get_version(void);

int	streq(const char *s1,int s1len,const char *s2);

#ifdef __cplusplus
}
#endif
#endif
