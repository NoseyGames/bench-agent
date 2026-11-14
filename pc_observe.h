#ifndef BENCH_PC_OBSERVE_H
#define BENCH_PC_OBSERVE_H
#include "pc_control.h"
/* Driver-internal entry point. The caller has selected/focused the window. */
gboolean pc_observe_window(PcSession *session,GError **error);
gboolean pc_observation_equal(const char *a,const char *b);
#endif
