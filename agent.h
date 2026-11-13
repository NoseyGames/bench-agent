#ifndef BENCH_AGENT_H
#define BENCH_AGENT_H
#include <glib.h>
#include <curl/curl.h>

/* Protocol ownership: parsed strings belong to Plan/Questions until cleared.
 * Plans contain relative UTF-8 text file replacements and real shell commands.
 * project.c enforces file boundaries; runner.c owns command processes. */
#define MAX_FILES 12
#define MAX_FILE_BYTES (128 * 1024)
#define MAX_RESPONSE_BYTES (2 * 1024 * 1024)
typedef struct { char *name, *content; } AgentFile;
typedef struct { char *kind, *command; guint timeout; } AgentCommand;
typedef struct {
    char *summary;
    GPtrArray *steps;
    GPtrArray *commands;  /* AgentCommand*, owned */
    GPtrArray *read_files; /* char*, owned; exclusive with edits/commands */
    guint count;
    AgentFile files[MAX_FILES];
} Plan;
typedef struct { guint count; char *text[3]; } Questions;

gboolean parse_questions(const char *json, Questions *out, GError **error);
gboolean parse_plan(const char *json, Plan *out, GError **error);
void questions_clear(Questions *q);
void plan_clear(Plan *p);
gboolean safe_filename(const char *name);
char *completion_content(const char *body, GError **error);
char *make_request(const char *model, const char *task,
                   const char *questions, const char *answers,
                   const char *context, const char *feedback);
/* Creates one new file, exclusively, without overwriting existing content. */
gboolean save_file(const char *dir, const AgentFile *file, GError **error);

/* The GTK main thread owns this handle and pumps it without blocking.
 * poll: 0 pending, 1 HTTP response available, -1 transport/setup failure.
 * body/error remain valid until api_clear. Cancel calls api_clear immediately. */
typedef struct {
    CURLM *multi;
    CURL *easy;
    struct curl_slist *headers;
    GString *body;
    char *payload;
    char curl_error[CURL_ERROR_SIZE];
    char *error;
    long status;
} Api;
gboolean api_start(Api *api, const char *key, char *owned_payload);
/* Same ownership as api_start. Local endpoints are restricted to loopback;
 * OpenAI remains fixed HTTPS. Local credentials are supplied independently. */
gboolean api_start_at(Api *api, const char *key, char *owned_payload,
                      const char *endpoint, gboolean json_mode);
int api_poll(Api *api);
void api_clear(Api *api);
#endif
