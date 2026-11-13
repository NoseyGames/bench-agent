#ifndef BENCH_PC_CONTROL_H
#define BENCH_PC_CONTROL_H
#include <glib.h>
/* Window-scoped Windows desktop input. Frames and action strings are owned by
 * the caller; no input occurs during parsing or before pc_execute(). */
typedef struct { char *kind,*text,*reason; int x,y,delta; } PcAction;
typedef struct { guint64 window; guint32 pid; int left,top,width,height,image_width,image_height; char *image; } PcSession;
char *pc_windows(GError **error);
gboolean pc_open(PcSession *s,const char *window,GError **error);
gboolean pc_capture(PcSession *s,GError **error);
gboolean pc_parse(const char *json,PcAction *a,GError **error);
gboolean pc_execute(PcSession *s,const PcAction *a,GError **error);
gboolean pc_cancelled(void);
void pc_clear(PcSession *s);
void pc_action_clear(PcAction *a);
char *pc_request(const char *model,const char *task,const char *answers,const PcSession *s,const char *history);
#endif
