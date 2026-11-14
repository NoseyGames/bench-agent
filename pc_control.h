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
    /* Owned compact UI Automation observation. No image is captured by
     * pc_observe(); thin means the provider exposed too little usable state. */
    char *structured;
    gboolean structured_thin;
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
gboolean pc_observe(PcSession *s,GError **error);
/* Safe batching guard: a click->type continuation requires an observed,
 * non-password edit/combobox focused at that same logical target point. */
gboolean pc_can_type_at(const PcSession *s,int x,int y);
/* Owned UIA runtime ID of a previously observed editable target, or NULL.
 * Geometry-only fallback IDs cannot authorize a batched type continuation. */
char *pc_edit_target_id(const PcSession *s,int x,int y);
gboolean pc_can_type_target(const PcSession *s,int x,int y,const char *id);
/* Internal subprocess entry point: dispatch --desktop-observe WINDOW:PID
 * before normal engine initialization. It never sends input or takes images. */
int pc_observe_worker(const char *window_id);
/* Call after approval and before input. Compares fresh structured state; an
 * explicitly captured image is also compared locally. WOULD_BLOCK replaces
 * stale state for replanning/new approval, without sending input. Other
 * failures preserve old state. Neither comparison proves desktop safety. */
gboolean pc_revalidate(PcSession *s,GError **error);
gboolean pc_parse(const char *json,PcAction *a,GError **error);
gboolean pc_execute(PcSession *s,const PcAction *a,GError **error);
gboolean pc_cancelled(void);
void pc_clear(PcSession *s);
void pc_action_clear(PcAction *a);
char *pc_request(const char *model,const char *task,const char *answers,const PcSession *s,const char *history);
#endif
