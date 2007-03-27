/* ex_docmd.c */
extern void do_exmode __ARGS((int improved));
extern int do_cmdline_cmd __ARGS((char_u *cmd));
extern int do_cmdline __ARGS((char_u *cmdline, char_u *(*getline)(int, void *, int), void *cookie, int flags));
extern int getline_equal __ARGS((char_u *(*getline)(int, void *, int), void *cookie, char_u *(*func)(int, void *, int)));
extern void *getline_cookie __ARGS((char_u *(*getline)(int, void *, int), void *cookie));
extern int checkforcmd __ARGS((char_u **pp, char *cmd, int len));
extern int cmd_exists __ARGS((char_u *name));
extern char_u *set_one_cmd_context __ARGS((expand_T *xp, char_u *buff));
extern char_u *skip_range __ARGS((char_u *cmd, int *ctx));
extern void ex_ni __ARGS((exarg_T *eap));
extern int expand_filename __ARGS((exarg_T *eap, char_u **cmdlinep, char_u **errormsgp));
extern void separate_nextcmd __ARGS((exarg_T *eap));
extern int ends_excmd __ARGS((int c));
extern char_u *find_nextcmd __ARGS((char_u *p));
extern char_u *check_nextcmd __ARGS((char_u *p));
extern char_u *get_command_name __ARGS((expand_T *xp, int idx));
extern void ex_comclear __ARGS((exarg_T *eap));
extern void uc_clear __ARGS((garray_T *gap));
extern char_u *get_user_commands __ARGS((expand_T *xp, int idx));
extern char_u *get_user_cmd_flags __ARGS((expand_T *xp, int idx));
extern char_u *get_user_cmd_nargs __ARGS((expand_T *xp, int idx));
extern char_u *get_user_cmd_complete __ARGS((expand_T *xp, int idx));
extern int parse_compl_arg __ARGS((char_u *value, int vallen, int *complp, long *argt, char_u **compl_arg));
extern void not_exiting __ARGS((void));
extern void tabpage_close __ARGS((int forceit));
extern void tabpage_close_other __ARGS((tabpage_T *tp, int forceit));
extern void ex_all __ARGS((exarg_T *eap));
extern void handle_drop __ARGS((int filec, char_u **filev, int split));
extern void alist_clear __ARGS((alist_T *al));
extern void alist_init __ARGS((alist_T *al));
extern void alist_unlink __ARGS((alist_T *al));
extern void alist_new __ARGS((void));
extern void alist_expand __ARGS((int *fnum_list, int fnum_len));
extern void alist_set __ARGS((alist_T *al, int count, char_u **files, int use_curbuf, int *fnum_list, int fnum_len));
extern void alist_add __ARGS((alist_T *al, char_u *fname, int set_fnum));
extern void alist_slash_adjust __ARGS((void));
extern void ex_splitview __ARGS((exarg_T *eap));
extern void tabpage_new __ARGS((void));
extern void do_exedit __ARGS((exarg_T *eap, win_T *old_curwin));
extern void free_cd_dir __ARGS((void));
extern void do_sleep __ARGS((long msec));
extern int vim_mkdir_emsg __ARGS((char_u *name, int prot));
extern FILE *open_exfile __ARGS((char_u *fname, int forceit, char *mode));
extern void update_topline_cursor __ARGS((void));
extern void exec_normal_cmd __ARGS((char_u *cmd, int remap, int silent));
extern char_u *eval_vars __ARGS((char_u *src, char_u *srcstart, int *usedlen, linenr_T *lnump, char_u **errormsg, int *escaped));
extern char_u *expand_sfile __ARGS((char_u *arg));
extern int put_eol __ARGS((FILE *fd));
extern int put_line __ARGS((FILE *fd, char *s));
extern void dialog_msg __ARGS((char_u *buff, char *format, char_u *fname));
/* vim: set ft=c : */
