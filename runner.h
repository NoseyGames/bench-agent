#ifndef BENCH_RUNNER_H
#define BENCH_RUNNER_H
#include <glib.h>
/* One command at a time. Caller owns Runner, polls on the UI thread, then clears.
 * Commands execute in cwd with the user's privileges (not a security sandbox).
 * stdin is closed; stdout/stderr are merged. No application API key is inherited.
 * Poll appends bytes to output, retains its last 256 KiB, and returns TRUE when
 * the shell exits. Cancel/timeout terminates the job/process group and is reaped
 * by subsequent polls. All handles are owned here, including on partial init. */
typedef struct {
    gboolean active, cancelled, timed_out, truncated;
    int exit_code;
    gint64 deadline;
    GString *output;
    guint64 output_bytes;
#ifdef G_OS_WIN32
    void *process, *job, *pipe;
#else
    int pid, pipe_fd;
#endif
} Runner;
gboolean runner_start(Runner *r,const char *cwd,const char *command,guint timeout_seconds,GError **e);
gboolean runner_poll(Runner *r);
void runner_cancel(Runner *r);
void runner_clear(Runner *r);
const char *runner_shell(void);
#endif
