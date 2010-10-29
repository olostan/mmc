#ifndef CMALLOC_H
#define	CMALLOC_H

#include <stdlib.h>

void  *chk_malloc(size_t size);
void  *chk_realloc(void *q,size_t size);
char *chk_strdup(const char *str);

#endif
