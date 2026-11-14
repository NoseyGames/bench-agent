#ifndef BENCH_PC_AGENT_H
#define BENCH_PC_AGENT_H
#include "pc_control.h"
#include <json-glib/json-glib.h>

#define PC_PLAN_MAX_ACTIONS 3
/* A plan is validated as one unit before the engine may request any approval.
 * Each action and expected string is owned. No parser captures or injects input. */
typedef struct {
    PcAction actions[PC_PLAN_MAX_ACTIONS];
    char *expected[PC_PLAN_MAX_ACTIONS];
    guint count;
} PcPlan;
void pc_plan_clear(PcPlan *plan);
char *pc_plan_action_json(const PcPlan *plan,guint index);

/* One owner (engine main thread). Responses retains opaque reasoning/tool items
 * unchanged; local Chat Completions keeps a bounded working state. Desktop
 * snapshots are memory-only and never cause a provider switch. */
typedef struct {
    gboolean responses;
    gboolean awaiting_observation;
    JsonArray *history;
    JsonNode *desktop;
    char *model,*task,*answers,*call_id;
    char *observation,*expected,*verification,*evidence,*outcome;
    char *last_action;
    guint frame, repeated, uncertain;
    gsize bytes;
} PcAgent;
void pc_agent_init(PcAgent *a,gboolean responses,const char *model,const char *task,const char *answers);
void pc_agent_clear(PcAgent *a);
/* Send structured state by default. attach_image is true only after the engine's
 * screenshot approval. Completes one plan with actual per-action driver results.
 * Responses appends deltas; local sends current compact state and last plan. */
gboolean pc_agent_observe(PcAgent *a,const PcSession *s,const char *result,gboolean attach_image,GError **error);
char *pc_agent_request(PcAgent *a,GError **error); /* caller owns payload */
/* Validate the complete HTTP body, closed plan shape, frame, all actions and
 * verification before changing out/history. Returns owned original plan JSON.
 * Empty actions are terminal. Nonempty batches are ordered move/click/type,
 * with at most one of each and no newline text. Other actions must stand alone. */
char *pc_agent_accept_plan(PcAgent *a,const char *body,PcPlan *out,GError **error);
#endif
