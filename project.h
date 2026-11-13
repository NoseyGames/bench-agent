#ifndef BENCH_PROJECT_H
#define BENCH_PROJECT_H
#include "agent.h"
/* Snapshot holds hashes only for files actually supplied to the model.
 * Edits to unseen or externally modified existing files fail rather than clobber.
 * Paths are project-relative, nested paths allowed, symlinks/reparse points denied.
 * Backups contain pre-edit bytes once per file per run; commands are NOT sandboxed
 * or rolled back by this file-edit mechanism. */
typedef struct {
    char *root, *run_dir;
    GHashTable *hashes;
    guint read_count, skipped;
} Project;
gboolean project_open(Project *p,const char *root,GError **e);
void project_clear(Project *p);
gboolean project_path_valid(const char *path);
char *project_context(Project *p,GError **e);
char *project_read(Project *p,const char *relative,GError **e);
gboolean project_check(Project *p,const Plan *plan,GError **e);
gboolean project_write(Project *p,const AgentFile *file,GError **e);
gboolean project_log(Project *p,const char *text,GError **e);
#endif
