#include "cmalloc.h"
#include "window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
extern void	win32_nomem(void);
#endif

/* checked malloc */
static void  fatal_error(const char *fmt,size_t v) {
  window_done();
#ifdef WIN32
  win32_nomem();
#else
  fprintf(stderr,"ERROR: ");
  fprintf(stderr,fmt,v);
  fprintf(stderr,"\n");
#endif
  exit(1);
}

void  *chk_malloc(size_t size) {
  void	*p=malloc(size);
  if (!p)
    fatal_error("can't malloc %d bytes",size);
  return p;
}

void  *chk_realloc(void *q,size_t size) {
  void	*p=realloc(q,size);
  if (!p)
    fatal_error("can't malloc %d bytes",size);
  return p;
}

char *chk_strdup(const char *str) {
  char	*p=strdup(str);
  if (!p)
    fatal_error("can't malloc %d byetes",strlen(str)+1);
  return p;
}

