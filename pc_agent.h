#ifndef BENCH_PC_AGENT_H
#define BENCH_PC_AGENT_H
#include "pc_control.h"
#include <json-glib/json-glib.h>

/* One owner (engine main thread). History is append-only and memory-only.
 * Responses retains opaque reasoning/tool items; local Chat Completions retains
 * assistant JSON and observations. No function here captures or injects input. */
typedef struct {
    gboolean responses;
    JsonArray *history;
    char *model,*task,*answers,*call_id;
    char *observation,*expected,*verification,*evidence,*outcome;
    char *last_action;
    guint frame, repeated, uncertain;
    gsize bytes;
} PcAgent;
void pc_agent_init(PcAgent *a,gboolean responses,const char *model,const char *task,const char *answers);
void pc_agent_clear(PcAgent *a);
/* Append a new frame and the actual driver result. Call only after approval.
 * Completes the outstanding function call, if any. Stops at a 32 MiB budget. */
gboolean pc_agent_observe(PcAgent *a,const PcSession *s,const char *result,GError **error);
char *pc_agent_request(PcAgent *a,GError **error); /* caller owns payload */
/* Validate raw HTTP body, frame binding, one tool call, verification and bounded
 * repeated actions. Returns owned action JSON; out belongs to caller. */
char *pc_agent_accept(PcAgent *a,const char *body,PcAction *out,GError **error);
#endif
