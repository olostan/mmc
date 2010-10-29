#ifndef SOCKET_H
#define SOCKET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

#include "Char.h"

#define	SCONN		1
#define	SEOF		2
#define	SLINE		3
#define	SPROMPT		4
#define	SECHOON		5
#define	SECHOOFF	6
#define	SIOERR		7
#define	SCLOSED		8
#define	SBELL		9

typedef void (*PSHANDLER)(int sock,int code,void *data,const Char *buf,size_t length);

void	swrite(int s,const void *ptr,int count,int raw);
void	sputc(int s,char c);
int	sconnect(char *addr,int port,char *laddr,int doproc,PSHANDLER hf,void *ha);
int	sconnect_pipe(char *cmd,int doproc,PSHANDLER hf,void *ha);
void	sclose(int s);
void	sosetlp(int s,int lp);

int	main_loop_iteration(void);
void	post_quit_message(void);
int	loop_finished(void);

void	sgetcounters(int s,int *rin,int *rout,int *pin);

void	set_lp_delay(int newdelay);

#ifdef WIN32
void	socket_cleanup(void);
void	soproc(int s,void *extbuf,int extlen);
int	StartExtCmd(char *cmd,int idx);
#else
#define socket_cleanup()
#endif

void	flush_socks(void);

int	sock_get_tcp_info(int s,int full,char *buf,int *bufsize);

#ifdef __cplusplus
}
#endif

#endif
