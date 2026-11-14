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
    /* Inventory fingerprints describe changes, never permission to overwrite.
     * Tracked paths are the 32 most recent explicit reads or successful writes. */
    GHashTable *inventory;
    GPtrArray *tracked;
    gboolean context_initialized, inventory_truncated;
} Project;
gboolean project_open(Project *p,const char *root,GError **e);
void project_clear(Project *p);
gboolean project_path_valid(const char *path);
char *project_context(Project *p,GError **e);
/* First call returns the existing snapshot; later calls return a bounded
 * inventory/delta and current tracked contents. Each replaces read authority
 * with only the contents actually returned. All returned strings are owned. */
char *project_context_delta(Project *p,GError **e);
gboolean project_track_file(Project *p,const char *relative,GError **e);
/* Root-bound host cache: paths/fingerprints only, never contents or permissions.
 * Import validates atomically; the next context reads actual files from disk. */
char *project_state_export(Project *p);
gboolean project_state_import(Project *p,const char *json,GError **e);
char *project_read(Project *p,const char *relative,GError **e);
gboolean project_check(Project *p,const Plan *plan,GError **e);
gboolean project_write(Project *p,const AgentFile *file,GError **e);
gboolean project_log(Project *p,const char *text,GError **e);
#endif
