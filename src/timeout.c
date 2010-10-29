#ifdef WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif
#include <stdlib.h>

#include "timeout.h"

#ifdef WIN32
ltime_t	gettime(void) {
	return GetTickCount();
}
#else
ltime_t	gettime(void) {
	struct timeval tv;
	gettimeofday(&tv,NULL);
	return (ltime_t)tv.tv_sec*1000+(tv.tv_usec/1000);
}
#endif

#define	NTM		32

static struct timeout {
	int	    next,prev;

	int	    inuse;
	ltime_t	    tm;
	PEVHANDLER  handler;
	void	    *data;
} tms[NTM];

int	head=-1;

int		stimeout(ltime_t when,PEVHANDLER handler,void *data) {
	int	i,j,k;

	for (i=0;i<NTM;i++)
		if (!tms[i].inuse)
			break;
	if (i==NTM)
		return -1;
	tms[i].tm=when;
	tms[i].handler=handler;
	tms[i].data=data;
	tms[i].inuse=1;
	for (j=head,k=-1;j>=0;k=j,j=tms[j].next)
		if (tms[j].tm>tms[i].tm)
			break;
	tms[i].next=j;
	tms[i].prev=k;
	if (j>=0)
	  tms[j].prev=i;
	if (k>=0)
	  tms[k].next=i;
	else
	  head=i;
	return i;
}

void	cancel_timeout(int id) {
	if (id<0 || id>=NTM || tms[id].inuse!=1)
		return;
	if (tms[id].prev>=0)
		tms[tms[id].prev].next=tms[id].next;
	if (tms[id].next>=0)
		tms[tms[id].next].prev=tms[id].prev;
	if (head==id)
		head=tms[id].next;
	tms[id].handler(1,tms[id].data);
	tms[id].inuse=0;
}

ltime_t	nearest_timeout() {
	ltime_t		tm;

	if (head<0)
		return -1;
	tm=tms[head].tm-gettime();
	return tm<0?0:tm;
}

void	process_timeouts(void) {
	ltime_t			ctm;
	int			h,i;

	if (head<0)
	  return;
	ctm=gettime();
	/* 1. slice out all callbacks that we should execute */
	for (i=head;i>=0 && ctm>=tms[i].tm;i=tms[i].next)
	  tms[i].inuse=2;
	if (i==head)
	  return;
	h=head;
	if (i>=0 && tms[i].prev>=0) {
	  tms[tms[i].prev].next=-1;
	  tms[i].prev=-1;
	}
	head=i;
	/* 2. execute the removed callbacks */
	for (i=h;i>=0;i=tms[i].next)
	  tms[i].handler(0,tms[i].data);
	/* 3. mark callbacks as free now */
	for (i=h;i>=0;i=tms[i].next)
	  tms[i].inuse=0;
}

void	delay(int ms) {
#ifdef WIN32
  Sleep(ms);
#else
  usleep(ms*1000);
#endif
}
