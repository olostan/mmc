#ifndef WINDOW_H
#define	WINDOW_H
#ifdef __cplusplus
extern "C" {
#endif

typedef void  (*WindowCB)(int w,const char *key,int len,void *data);

const char *window_init(void);
void	window_done(void);

void	window_getsize(int *width,int *height,int *inputwidth);

int	window_open(void);
void	window_close(int w);
void	window_flush(void);

void	window_set_status_mode(int mode,int height);

void	window_output_ctext(int w,int flag,const char *ctext,int len);
void	window_output_text(int w,int flag,const char *text,int len,int color);

void	window_output_cstatus(int x,int y,const char *ctext,int len);
void	window_output_status(int x,int y,const char *text,int len,int color);

void	window_output_cinput(int x,const char *ctext,int len);
void	window_output_input(int x,const char *text,int len,int color);
void	window_insert_input(int x,int n);
void	window_delete_input(int x,int n);
void	window_move_icursor(int x);
void	window_move_icursor_left(void);
void	window_move_icursor_right(void);
void	window_deol_input(void);
int	window_input_pos(void);

void	window_set_cb(WindowCB cb,void *data);
void	window_set_confvar(int id,int var);
void	window_playsound(const char *name);

void	window_switch(int w);
void	window_sbup(int amount);
void	window_sbdown(int amount);
void	window_redraw(void);

int	window_fetchline(int w,int idx,const char **text,int *len,int *flag);

enum {
  WCF_SBSIZE,
  WCF_INPUTBG,
  WCF_STATUSBG,
  WCF_TEXTBG,
  WCF_SBSTATUSBG,
  WCF_SLOWSCROLL
};

enum {
  WMSG_ACT
};

#ifdef __cplusplus
}
#endif
#endif
