/* $Id: timeout.h,v 1.8 2001/07/12 00:03:00 mike Exp $ */
#ifndef EVENT_H
#define EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

/* i'm fed up with that struct timeval inconvenience */
/* most compilers provide 64bit integer types nowdays */
#ifdef WIN32
typedef __int64			ltime_t;
#else
typedef long long int	ltime_t; /* a gcc extension of course */
#endif

ltime_t	gettime(void);


typedef void	(*PEVHANDLER)(int cancel,void *data);

int		stimeout(ltime_t when,PEVHANDLER handler,void *data);
void	cancel_timeout(int id);

ltime_t	nearest_timeout(void);
void	process_timeouts(void);

void	delay(int ms);

#ifdef __cplusplus
}
#endif

#endif
