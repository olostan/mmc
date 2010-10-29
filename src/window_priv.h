#ifndef WINDOW_PRIV_H
#define WINDOW_PRIV_H

struct Window;

void	window_process_key(const char *key,int len);
int	window_getline(struct Window *w,int idx,const char **text,int *len);
void	window_resize(int newwidth,int newheight);

#endif
