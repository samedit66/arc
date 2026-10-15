#ifndef ARC_INTERNAL_PROGRESS_H
#define ARC_INTERNAL_PROGRESS_H
#include "arc.h"

typedef struct {
    ArcObserver observer;
    ArcProgress snapshot;
} ProgressState;

void ar_progress_phase(ProgressState *state, ArcProgressPhase phase);
ArcResult ar_progress_entry(ProgressState *state, const char *path, uint64_t size, int known);
ArcResult ar_progress_bytes(ProgressState *state, uint64_t bytes);
#endif
