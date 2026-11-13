#ifndef BENCH_PC_CONTROL_H
#define BENCH_PC_CONTROL_H
#include <glib.h>
/* Window-scoped Windows desktop input. Frames and action strings are owned by
 * the caller; no input occurs during parsing or before pc_execute(). */
typedef struct { char *kind,*text,*reason; int x,y,delta; } PcAction;
typedef struct {
    guint64 window;
    guint32 pid;
    int left,top,width,height,image_width,image_height;
    char *image;
    /* Owned packed RGB pixels and client bounds within the scaled screenshot.
     * Retained only for a local preflight comparison; never persisted. */
    guchar *pixels;
    int client_x,client_y,client_width,client_height;
    gint64 captured_us;
} PcSession;
#define PC_OBSERVATION_MAX_AGE_US (120 * G_USEC_PER_SEC)
char *pc_windows(GError **error);
gboolean pc_open(PcSession *s,const char *window,GError **error);
gboolean pc_capture(PcSession *s,GError **error);
/* Call after approval and before input. Captures locally and compares against
 * the model's frame. On G_IO_ERROR_WOULD_BLOCK the old proposal is stale and s
 * contains the new frame: replan and obtain a new approval, without input.
 * Other failures preserve the old frame. This is a conservative pixel heuristic,
 * not proof that a control retained its meaning or that the desktop is safe. */
gboolean pc_revalidate(PcSession *s,GError **error);
gboolean pc_parse(const char *json,PcAction *a,GError **error);
gboolean pc_execute(PcSession *s,const PcAction *a,GError **error);
gboolean pc_cancelled(void);
void pc_clear(PcSession *s);
void pc_action_clear(PcAction *a);
char *pc_request(const char *model,const char *task,const char *answers,const PcSession *s,const char *history);
#endif
