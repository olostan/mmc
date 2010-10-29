#ifndef	OUTPUT_H
#define	OUTPUT_H

/* terminal abstraction */

void	out_setcolor(int fg,int bg);
void	out_cwrite(const char *ctext,int len,int bg);
void	out_rawrite(const char *text,int len);
void	out_inschars(int n);
void	out_delchars(int n);
void	out_setscroll(int top,int bot);
void	out_movecursor(int x,int y);
void	out_clearscr(void);
void	out_clearline(void);
void	out_update_winsize(void);
void	out_drawlines(void *w,int count,int bg);
void	out_process_input(void);
const char *out_setup(int how);
void	out_sigcheck(void);
void	out_playsound(const char *filename);
void	out_flush(void);
void	out_addkey(const char *key,const char *name);

#endif
